const fs = require('fs');
const path = require('path');

const CAR_ID_RE = /^car_(10|[1-9])$/;
const FLEET_FEATURES = new Set(['ADAS', 'Navigation', 'Energy']);

const UPDATE_MODES = new Set([
  'READY',
  'COMPLETED',
  'FAILED',
  'DOWNLOADING',
  'READY_TO_INSTALL',
  'INSTALLING',
]);

const DOWNLOAD_STATUSES = new Set([
  'not_started',
  'downloading',
  'downloaded',
  'failed',
]);

const INSTALL_STATUSES = new Set([
  'not_started',
  'installing',
  'installed',
  'failed',
]);

const WRITABLE_OTA_FIELDS = new Set([
  'update_mode',
  'download_status',
  'install_status',
  'driver_approved',
]);

const PREPARE_DEMO_READY = {
  update_mode: 'READY',
  download_status: 'downloaded',
  install_status: 'not_started',
  driver_approved: true,
};

function validateCarId(carId) {
  const s = String(carId || '').trim();
  if (!CAR_ID_RE.test(s)) {
    const err = new Error('car_id must be car_1 .. car_10');
    err.statusCode = 400;
    throw err;
  }
  return s;
}

function validateFeature(feature) {
  const f = String(feature || 'ADAS').trim();
  if (!FLEET_FEATURES.has(f)) {
    const err = new Error(
      `feature must be one of: ${[...FLEET_FEATURES].join(', ')}`,
    );
    err.statusCode = 400;
    throw err;
  }
  return f;
}

function safeReadJson(filePath) {
  try {
    return { ok: true, data: JSON.parse(fs.readFileSync(filePath, 'utf8')) };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

function safeWriteJsonAtomic(filePath, data) {
  const dir = path.dirname(filePath);
  fs.mkdirSync(dir, { recursive: true });
  const tmp = `${filePath}.tmp.${process.pid}.${Date.now()}`;
  fs.writeFileSync(tmp, `${JSON.stringify(data, null, 2)}\n`, 'utf8');
  fs.renameSync(tmp, filePath);
}

function resolveStatePath(deployRoot, carId) {
  const base = path.resolve(deployRoot, 'state');
  const fp = path.resolve(base, `${carId}.json`);
  if (fp !== base && !fp.startsWith(base + path.sep)) {
    const err = new Error('invalid state path');
    err.statusCode = 400;
    throw err;
  }
  return fp;
}

function buildSafetySummary(state, feature) {
  const ota = state?.ota && typeof state.ota === 'object' ? state.ota : {};
  const feat =
    state?.features?.[feature] && typeof state.features[feature] === 'object'
      ? state.features[feature]
      : {};
  const killSwitch = Boolean(feat.kill_switch);

  const demoReasons = [];
  if (String(ota.update_mode || '') !== 'READY') {
    demoReasons.push(
      `update_mode must be READY (current=${ota.update_mode || '—'})`,
    );
  }
  if (!ota.driver_approved) {
    demoReasons.push('driver_approved must be true');
  }
  if (killSwitch) {
    demoReasons.push(`kill_switch is active for ${feature}`);
  }

  const extra = {
    vehicle_mode: ota.vehicle_mode ?? null,
    ignition: ota.ignition ?? null,
    gear: ota.gear ?? null,
    speed: ota.speed ?? null,
    parking_brake: ota.parking_brake ?? null,
    battery: ota.battery ?? null,
    network: ota.network ?? null,
    target_system_running: ota.target_system_running ?? null,
    ota_phase: ota.ota_phase ?? null,
  };

  const trackedExtra = Object.fromEntries(
    Object.entries(extra).map(([k, v]) => [
      k,
      v === null || v === undefined ? 'not tracked' : v,
    ]),
  );

  return {
    car_id: state?.car_id || '',
    feature,
    ota: {
      update_mode: String(ota.update_mode || ''),
      download_status: String(ota.download_status || ''),
      install_status: String(ota.install_status || ''),
      driver_approved: Boolean(ota.driver_approved),
    },
    extra: trackedExtra,
    kill_switch: killSwitch,
    kill_reason: feat.kill_reason || '',
    demo_ready: demoReasons.length === 0,
    demo_ready_label: demoReasons.length === 0 ? 'YES' : 'NO',
    demo_ready_reasons: demoReasons,
    state_file: `deploy/state/${state?.car_id || '?'}.json`,
  };
}

function applyPatchToState(state, patch) {
  if (!state.ota || typeof state.ota !== 'object') state.ota = {};
  const updated = [];
  for (const [key, raw] of Object.entries(patch)) {
    if (!WRITABLE_OTA_FIELDS.has(key)) continue;
    if (key === 'driver_approved') {
      state.ota.driver_approved = Boolean(raw);
      updated.push(key);
      continue;
    }
    const val = String(raw || '').trim();
    if (key === 'update_mode' && !UPDATE_MODES.has(val)) {
      const err = new Error(`invalid update_mode: ${val}`);
      err.statusCode = 400;
      throw err;
    }
    if (key === 'download_status' && !DOWNLOAD_STATUSES.has(val)) {
      const err = new Error(`invalid download_status: ${val}`);
      err.statusCode = 400;
      throw err;
    }
    if (key === 'install_status' && !INSTALL_STATUSES.has(val)) {
      const err = new Error(`invalid install_status: ${val}`);
      err.statusCode = 400;
      throw err;
    }
    state.ota[key] = val;
    updated.push(key);
  }
  return updated;
}

function registerVehicleSafetyRoutes(app, deps) {
  const { DEPLOY_ROOT, syncRuntimeCmdForCars } = deps;
  const STATE_DIR = path.join(DEPLOY_ROOT, 'state');

  app.get('/api/vehicles/:carId/safety-state', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.query.feature);
      const fp = resolveStatePath(DEPLOY_ROOT, carId);
      if (!fs.existsSync(fp)) {
        res
          .status(404)
          .json({
            ok: false,
            error: `missing deploy/state/${carId}.json`,
            car_id: carId,
          });
        return;
      }
      const parsed = safeReadJson(fp);
      if (!parsed.ok) {
        res.status(500).json({ ok: false, error: parsed.error, car_id: carId });
        return;
      }
      res.json({
        ok: true,
        execution: false,
        ...buildSafetySummary(parsed.data, feature),
      });
    } catch (err) {
      res
        .status(err.statusCode || 500)
        .json({ ok: false, error: err.message || String(err) });
    }
  });

  app.post('/api/vehicles/:carId/safety-state', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.body?.feature || req.query?.feature);
      const fp = resolveStatePath(DEPLOY_ROOT, carId);

      let state;
      if (fs.existsSync(fp)) {
        const parsed = safeReadJson(fp);
        if (!parsed.ok) {
          res
            .status(500)
            .json({ ok: false, error: parsed.error, car_id: carId });
          return;
        }
        state = parsed.data;
      } else {
        res
          .status(404)
          .json({
            ok: false,
            error: `missing deploy/state/${carId}.json`,
            car_id: carId,
          });
        return;
      }

      let patch = {};
      if (String(req.body?.preset || '').trim() === 'prepare_demo_ready') {
        patch = { ...PREPARE_DEMO_READY };
      } else if (req.body && typeof req.body === 'object') {
        for (const key of WRITABLE_OTA_FIELDS) {
          if (req.body[key] !== undefined) patch[key] = req.body[key];
        }
      }

      if (!Object.keys(patch).length) {
        res.status(400).json({
          ok: false,
          error:
            'no writable fields provided (use update_mode, download_status, install_status, driver_approved, or preset=prepare_demo_ready)',
        });
        return;
      }

      const updatedFields = applyPatchToState(state, patch);
      safeWriteJsonAtomic(fp, state);
      console.log(
        `[SAFETY] updated deploy/state/${carId}.json fields=${updatedFields.join(',')}`,
      );

      if (typeof syncRuntimeCmdForCars === 'function') {
        syncRuntimeCmdForCars([carId]);
      }

      res.json({
        ok: true,
        execution: true,
        car_id: carId,
        updated_fields: updatedFields,
        ...buildSafetySummary(state, feature),
      });
    } catch (err) {
      res
        .status(err.statusCode || 500)
        .json({ ok: false, error: err.message || String(err) });
    }
  });
}

module.exports = {
  registerVehicleSafetyRoutes,
  buildSafetySummary,
  PREPARE_DEMO_READY,
};
