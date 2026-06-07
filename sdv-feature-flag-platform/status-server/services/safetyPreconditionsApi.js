/**
 * OTA Safety Preconditions — source-agnostic evaluator.
 *
 * Current demo: values are read from deploy/state/*.json (`ota` fields + optional `safety` metadata).
 * Future: populate the same normalized snapshot from vehicle-signal / MQTT / Signal I/F and reuse
 * evaluateSafetyPreconditions() unchanged.
 */
const fs = require('fs');
const path = require('path');

const CAR_ID_RE = /^car_(10|[1-9])$/;
const FLEET_FEATURES = new Set(['ADAS', 'Navigation', 'Energy']);

const VALID_SOURCES = new Set([
  'state-json',
  'vehicle-signal',
  'manual-demo',
  'unknown',
]);

const PREPARE_DEMO_READY_VALUES = {
  speed: 0,
  gear: 'P',
  ignition: 'OFF',
  battery: 75,
  driver_approved: true,
  network: 'CONNECTED',
  vehicle_mode: 'PARKED',
  parking_brake: 'ON',
  update_mode: 'READY',
  download_status: 'downloaded',
  install_status: 'not_started',
  target_system_running: false,
};

const PREPARE_DEMO_READY_OTA = {
  update_mode: 'READY',
  download_status: 'downloaded',
  install_status: 'not_started',
  driver_approved: true,
  speed: 0,
  gear: 'P',
  ignition: 'OFF',
  battery: 75,
  network: 'CONNECTED',
  vehicle_mode: 'PARKED',
  parking_brake: 'ON',
  target_system_running: false,
};

let cachedThresholds = null;

function getDefaultSafetyThresholds() {
  if (cachedThresholds) return { ...cachedThresholds };
  const configPath = path.join(__dirname, '..', 'config', 'safetyThresholds.json');
  try {
    cachedThresholds = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  } catch {
    cachedThresholds = {
      speed: 0,
      gear: 'P',
      ignition: 'OFF',
      min_battery: 30,
      driver_approved: true,
      network: 'CONNECTED',
      update_mode: 'READY',
      vehicle_mode: 'PARKED',
      parking_brake: 'ON',
      target_system_running: false,
    };
  }
  return { ...cachedThresholds };
}

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

function appendFleetHistory(deployRoot, record) {
  const historyPath = path.join(deployRoot, 'history', 'fleet_history.jsonl');
  fs.mkdirSync(path.dirname(historyPath), { recursive: true });
  const row = {
    timestamp: new Date().toISOString(),
    scope: 'vehicle',
    ...record,
  };
  fs.appendFileSync(historyPath, `${JSON.stringify(row)}\n`, 'utf8');
  return historyPath;
}

function readOtaValues(state) {
  const ota = state?.ota && typeof state.ota === 'object' ? state.ota : {};
  return {
    speed: Number(ota.speed ?? 0),
    gear: String(ota.gear ?? '').trim().toUpperCase(),
    ignition: String(ota.ignition ?? '').trim().toUpperCase(),
    battery: Number(ota.battery ?? 0),
    driver_approved: Boolean(ota.driver_approved),
    network: String(ota.network ?? '').trim().toUpperCase(),
    update_mode: String(ota.update_mode ?? '').trim().toUpperCase(),
    vehicle_mode: String(ota.vehicle_mode ?? '').trim().toUpperCase(),
    parking_brake: String(ota.parking_brake ?? '').trim().toUpperCase(),
    target_system_running: Boolean(ota.target_system_running),
    download_status: String(ota.download_status ?? ''),
    install_status: String(ota.install_status ?? ''),
  };
}

function resolveSource(state) {
  const src = String(state?.safety?.source || '').trim();
  if (VALID_SOURCES.has(src)) return src;
  return 'state-json';
}

/**
 * Normalize raw state / patch into a source-tagged safety snapshot.
 * @param {object} raw — { ota, safety, car_id }
 * @param {string} source
 */
function normalizeSafetySnapshot(raw, source = 'state-json') {
  const values = readOtaValues(raw);
  const src = VALID_SOURCES.has(source) ? source : resolveSource(raw);
  return {
    car_id: String(raw?.car_id || '').trim(),
    source: src,
    timestamp: new Date().toISOString(),
    values,
  };
}

function loadSafetySnapshot(deployRoot, carId) {
  const fp = resolveStatePath(deployRoot, carId);
  if (!fs.existsSync(fp)) {
    const err = new Error(`missing deploy/state/${carId}.json`);
    err.statusCode = 404;
    throw err;
  }
  const parsed = safeReadJson(fp);
  if (!parsed.ok) {
    const err = new Error(parsed.error);
    err.statusCode = 500;
    throw err;
  }
  const state = parsed.data;
  if (!state.car_id) state.car_id = carId;
  return normalizeSafetySnapshot(state, resolveSource(state));
}

function buildCheckResults(snapshot, thresholds, options = {}) {
  const v = snapshot.values || {};
  const t = thresholds || getDefaultSafetyThresholds();
  const feature = options.feature || 'ADAS';
  const killSwitch = Boolean(options.kill_switch);

  const checks = [
    {
      key: 'speed',
      label: 'Vehicle speed',
      actual: v.speed,
      required: String(t.speed),
      passed: Number(v.speed) === Number(t.speed),
      reason: Number(v.speed) !== Number(t.speed)
        ? 'Vehicle must be stopped before OTA action.'
        : undefined,
    },
    {
      key: 'gear',
      label: 'Gear',
      actual: v.gear || '—',
      required: String(t.gear).toUpperCase(),
      passed: v.gear === String(t.gear).toUpperCase(),
      reason:
        v.gear !== String(t.gear).toUpperCase()
          ? `Gear must be ${t.gear} before OTA action.`
          : undefined,
    },
    {
      key: 'ignition',
      label: 'Ignition',
      actual: v.ignition || '—',
      required: String(t.ignition).toUpperCase(),
      passed: v.ignition === String(t.ignition).toUpperCase(),
      reason:
        v.ignition !== String(t.ignition).toUpperCase()
          ? `Ignition must be ${t.ignition}.`
          : undefined,
    },
    {
      key: 'battery',
      label: 'Battery',
      actual: v.battery,
      required: `>= ${t.min_battery}%`,
      passed: Number(v.battery) >= Number(t.min_battery),
      reason:
        Number(v.battery) < Number(t.min_battery)
          ? `Battery must be at least ${t.min_battery}%.`
          : undefined,
    },
    {
      key: 'driver_approved',
      label: 'Driver approval',
      actual: v.driver_approved,
      required: String(Boolean(t.driver_approved)),
      passed: Boolean(v.driver_approved) === Boolean(t.driver_approved),
      reason: !v.driver_approved ? 'Driver approval is required.' : undefined,
    },
    {
      key: 'network',
      label: 'Network',
      actual: v.network || '—',
      required: String(t.network).toUpperCase(),
      passed: v.network === String(t.network).toUpperCase(),
      reason:
        v.network !== String(t.network).toUpperCase()
          ? `Network must be ${t.network}.`
          : undefined,
    },
    {
      key: 'update_mode',
      label: 'OTA update mode',
      actual: v.update_mode || '—',
      required: String(t.update_mode).toUpperCase(),
      passed: v.update_mode === String(t.update_mode).toUpperCase(),
      reason:
        v.update_mode !== String(t.update_mode).toUpperCase()
          ? `update_mode must be ${t.update_mode} for campaign demo.`
          : undefined,
    },
    {
      key: 'vehicle_mode',
      label: 'Vehicle mode',
      actual: v.vehicle_mode || '—',
      required: String(t.vehicle_mode).toUpperCase(),
      passed: v.vehicle_mode === String(t.vehicle_mode).toUpperCase(),
      reason:
        v.vehicle_mode !== String(t.vehicle_mode).toUpperCase()
          ? `vehicle_mode must be ${t.vehicle_mode}.`
          : undefined,
    },
    {
      key: 'parking_brake',
      label: 'Parking brake',
      actual: v.parking_brake || '—',
      required: String(t.parking_brake).toUpperCase(),
      passed: v.parking_brake === String(t.parking_brake).toUpperCase(),
      reason:
        v.parking_brake !== String(t.parking_brake).toUpperCase()
          ? `parking_brake must be ${t.parking_brake}.`
          : undefined,
    },
    {
      key: 'target_system_running',
      label: 'Target system running',
      actual: v.target_system_running,
      required: String(Boolean(t.target_system_running)),
      passed:
        Boolean(v.target_system_running) === Boolean(t.target_system_running),
      reason: v.target_system_running
        ? 'Target system must not be running during OTA.'
        : undefined,
    },
  ];

  if (killSwitch) {
    checks.push({
      key: 'kill_switch',
      label: `Kill switch (${feature})`,
      actual: true,
      required: 'false',
      passed: false,
      reason: `kill_switch is active for ${feature}.`,
    });
  }

  const failed_checks = checks.filter((c) => !c.passed).map((c) => c.key);
  return {
    ready: failed_checks.length === 0,
    source: snapshot.source,
    checks,
    failed_checks,
  };
}

function evaluateSafetyPreconditions(snapshot, thresholds, options = {}) {
  return buildCheckResults(snapshot, thresholds, options);
}

function persistSafetyEvaluation(state, evaluation) {
  if (!state.safety || typeof state.safety !== 'object') {
    state.safety = {};
  }
  state.safety.last_evaluation = {
    ready: evaluation.ready,
    evaluated_at: new Date().toISOString(),
    failed_checks: evaluation.failed_checks,
    source: evaluation.source,
  };
}

function buildSafetySummary(deployRoot, carId, feature = 'ADAS') {
  const fp = resolveStatePath(deployRoot, carId);
  const parsed = safeReadJson(fp);
  if (!parsed.ok) {
    return { ok: false, error: parsed.error, car_id: carId };
  }
  const state = parsed.data;
  const snapshot = normalizeSafetySnapshot(state, resolveSource(state));
  const feat =
    state?.features?.[feature] && typeof state.features[feature] === 'object'
      ? state.features[feature]
      : {};
  const evaluation = evaluateSafetyPreconditions(snapshot, getDefaultSafetyThresholds(), {
    feature,
    kill_switch: Boolean(feat.kill_switch),
  });

  return {
    ok: true,
    car_id: carId,
    feature,
    snapshot,
    evaluation,
    ready: evaluation.ready,
    source: snapshot.source,
    source_label: sourceDisplayLabel(snapshot.source),
    checks: evaluation.checks,
    failed_checks: evaluation.failed_checks,
    thresholds: getDefaultSafetyThresholds(),
    state_file: `deploy/state/${carId}.json`,
    simulation_notice:
      'Current safety values are simulated from deploy state JSON for demo. The same evaluator can be extended to real vehicle signals later.',
    simulation_notice_ko:
      '현재 안전 조건 값은 실제 차량 센서가 아니라 데모용 state JSON 값을 기반으로 시뮬레이션됩니다. 향후 실제 차량 신호와 연결해도 동일한 판단 로직을 재사용할 수 있습니다.',
  };
}

function sourceDisplayLabel(source) {
  switch (source) {
    case 'manual-demo':
      return 'Manual demo (Prepare Demo READY)';
    case 'vehicle-signal':
      return 'Vehicle signal';
    case 'state-json':
      return 'State JSON simulation';
    default:
      return 'Unknown';
  }
}

function applySafetyPatchToState(state, patch, source) {
  if (!state.ota || typeof state.ota !== 'object') state.ota = {};

  const otaKeys = new Set([
    'speed',
    'gear',
    'ignition',
    'battery',
    'driver_approved',
    'network',
    'update_mode',
    'download_status',
    'install_status',
    'vehicle_mode',
    'parking_brake',
    'target_system_running',
  ]);

  const updated = [];
  for (const [key, raw] of Object.entries(patch)) {
    if (!otaKeys.has(key)) continue;
    if (key === 'driver_approved' || key === 'target_system_running') {
      state.ota[key] = Boolean(raw);
    } else if (key === 'speed' || key === 'battery') {
      state.ota[key] = Number(raw);
    } else if (typeof raw === 'string') {
      state.ota[key] = raw;
    } else {
      state.ota[key] = raw;
    }
    updated.push(key);
  }

  const src = VALID_SOURCES.has(source) ? source : 'manual-demo';
  if (!state.safety || typeof state.safety !== 'object') state.safety = {};
  state.safety.source = src;
  state.safety.updated_at = new Date().toISOString();
  state.safety.values = readOtaValues(state);

  return updated;
}

function updateSafetyState(deployRoot, carId, patch, source = 'manual-demo') {
  const fp = resolveStatePath(deployRoot, carId);
  const parsed = safeReadJson(fp);
  if (!parsed.ok) {
    const err = new Error(parsed.error);
    err.statusCode = 500;
    throw err;
  }
  const state = parsed.data;
  const updated = applySafetyPatchToState(state, patch, source);
  if (!updated.length) {
    const err = new Error('no safety fields to update');
    err.statusCode = 400;
    throw err;
  }
  const snapshot = normalizeSafetySnapshot(state, source);
  const evaluation = evaluateSafetyPreconditions(snapshot, getDefaultSafetyThresholds());
  persistSafetyEvaluation(state, evaluation);
  safeWriteJsonAtomic(fp, state);
  return { state, updated, snapshot, evaluation };
}

function prepareDemoReady(deployRoot, carId, options = {}) {
  const source = options.source || 'manual-demo';
  const fp = resolveStatePath(deployRoot, carId);
  const parsed = safeReadJson(fp);
  if (!parsed.ok) {
    const err = new Error(parsed.error);
    err.statusCode = 500;
    throw err;
  }
  const state = parsed.data;
  const updated = applySafetyPatchToState(
    state,
    { ...PREPARE_DEMO_READY_OTA },
    source,
  );
  const snapshot = normalizeSafetySnapshot(state, source);
  const evaluation = evaluateSafetyPreconditions(snapshot, getDefaultSafetyThresholds(), {
    feature: options.feature || 'ADAS',
    kill_switch: Boolean(state?.features?.[options.feature || 'ADAS']?.kill_switch),
  });
  persistSafetyEvaluation(state, evaluation);
  safeWriteJsonAtomic(fp, state);

  appendFleetHistory(deployRoot, {
    action: 'safety-prepare-demo-ready',
    car_id: carId,
    source,
    ready: evaluation.ready,
    values: snapshot.values,
    requested_action: options.action || null,
    result: evaluation.ready ? 'success' : 'partial',
  });

  return { state, updated, snapshot, evaluation };
}

function assertSafetyReady(deployRoot, carId, options = {}) {
  const feature = validateFeature(options.feature || 'ADAS');
  const action = String(options.action || 'ota-action').trim();
  const summary = buildSafetySummary(deployRoot, carId, feature);

  if (!summary.ok) {
    return {
      ok: false,
      ready: false,
      car_id: carId,
      action,
      source: 'unknown',
      message: summary.error || 'Safety state unavailable.',
      failed_checks: [],
    };
  }

  if (!summary.ready) {
    appendFleetHistory(deployRoot, {
      action: 'safety-blocked',
      car_id: carId,
      requested_action: action,
      source: summary.source,
      ready: false,
      failed_checks: summary.failed_checks,
      result: 'blocked',
    });
    return {
      ok: false,
      ready: false,
      car_id: carId,
      action,
      source: summary.source,
      message: 'Safety preconditions are not READY.',
      failed_checks: summary.failed_checks,
      checks: summary.checks,
      next_action: 'Click Prepare Demo READY or adjust safety values.',
    };
  }

  return {
    ok: true,
    ready: true,
    car_id: carId,
    action,
    source: summary.source,
    message: `Safety preconditions are READY for ${action}.`,
    checks: summary.checks,
    failed_checks: [],
  };
}

function assertSafetyReadyForCars(deployRoot, carIds, options = {}) {
  const ids = [...new Set((carIds || []).map((id) => String(id || '').trim()).filter(Boolean))];
  const results = ids.map((carId) => {
    try {
      validateCarId(carId);
      return assertSafetyReady(deployRoot, carId, options);
    } catch (err) {
      return {
        ok: false,
        ready: false,
        car_id: carId,
        message: err.message,
        failed_checks: [],
      };
    }
  });
  const blocked = results.filter((r) => !r.ready);
  return {
    ok: blocked.length === 0,
    ready: blocked.length === 0,
    results,
    blocked,
    failed_checks: [...new Set(blocked.flatMap((b) => b.failed_checks || []))],
    message:
      blocked.length === 0
        ? `Safety preconditions are READY for ${options.action || 'ota-action'}.`
        : 'Safety preconditions are not READY for one or more vehicles.',
  };
}

function registerSafetyPreconditionsRoutes(app, deps) {
  const { DEPLOY_ROOT, syncRuntimeCmdForCars } = deps;

  app.get('/api/fleet/safety/:carId', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.query.feature);
      const summary = buildSafetySummary(DEPLOY_ROOT, carId, feature);
      if (!summary.ok) {
        res.status(404).json(summary);
        return;
      }
      res.json({ ok: true, execution: false, ...summary });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.post('/api/fleet/safety/:carId/update', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.body?.feature || req.query?.feature);
      const source = String(req.body?.source || 'manual-demo').trim();
      const patch = { ...(req.body || {}) };
      delete patch.feature;
      delete patch.source;

      const { updated, snapshot, evaluation } = updateSafetyState(
        DEPLOY_ROOT,
        carId,
        patch,
        source,
      );

      appendFleetHistory(DEPLOY_ROOT, {
        action: 'safety-update',
        car_id: carId,
        source: snapshot.source,
        ready: evaluation.ready,
        values: snapshot.values,
        updated_fields: updated,
        result: 'success',
      });

      if (typeof syncRuntimeCmdForCars === 'function') {
        syncRuntimeCmdForCars([carId]);
      }

      res.json({
        ok: true,
        execution: true,
        car_id: carId,
        updated_fields: updated,
        ...buildSafetySummary(DEPLOY_ROOT, carId, feature),
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.post('/api/fleet/safety/:carId/prepare-demo-ready', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.body?.feature || req.query?.feature);
      const action = String(req.body?.action || req.body?.for_action || '').trim();

      const { updated, evaluation } = prepareDemoReady(DEPLOY_ROOT, carId, {
        feature,
        action: action || undefined,
      });

      if (typeof syncRuntimeCmdForCars === 'function') {
        syncRuntimeCmdForCars([carId]);
      }

      res.json({
        ok: true,
        execution: true,
        car_id: carId,
        updated_fields: updated,
        ready: evaluation.ready,
        ...buildSafetySummary(DEPLOY_ROOT, carId, feature),
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.post('/api/fleet/safety/:carId/assert-ready', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.body?.feature || req.query?.feature);
      const action = String(req.body?.action || 'ota-action').trim();
      const result = assertSafetyReady(DEPLOY_ROOT, carId, { feature, action });
      res.status(result.ok ? 200 : 409).json(result);
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });
}

module.exports = {
  registerSafetyPreconditionsRoutes,
  loadSafetySnapshot,
  normalizeSafetySnapshot,
  evaluateSafetyPreconditions,
  getDefaultSafetyThresholds,
  prepareDemoReady,
  updateSafetyState,
  buildSafetySummary,
  assertSafetyReady,
  assertSafetyReadyForCars,
  PREPARE_DEMO_READY_VALUES,
  PREPARE_DEMO_READY_OTA,
  sourceDisplayLabel,
};
