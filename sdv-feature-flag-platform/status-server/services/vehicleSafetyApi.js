const {
  prepareDemoReady,
  updateSafetyState,
  buildSafetySummary,
  PREPARE_DEMO_READY_OTA,
} = require('./safetyPreconditionsApi');

const FLEET_FEATURES = new Set(['ADAS', 'Navigation', 'Energy']);
const CAR_ID_RE = /^car_(10|[1-9])$/;

const WRITABLE_OTA_FIELDS = new Set([
  'update_mode',
  'download_status',
  'install_status',
  'driver_approved',
  'speed',
  'gear',
  'ignition',
  'battery',
  'network',
  'vehicle_mode',
  'parking_brake',
  'target_system_running',
]);

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

/** Backward-compatible summary for legacy dashboard fields. */
function buildLegacySafetySummary(summary) {
  if (!summary?.ok) return summary;
  const ota = summary.snapshot?.values || {};
  const demoReasons = (summary.checks || [])
    .filter((c) => !c.passed)
    .map((c) => c.reason || `${c.label}: required ${c.required}, got ${c.actual}`);

  return {
    car_id: summary.car_id,
    feature: summary.feature,
    ota: {
      update_mode: String(ota.update_mode || ''),
      download_status: String(ota.download_status || ''),
      install_status: String(ota.install_status || ''),
      driver_approved: Boolean(ota.driver_approved),
    },
    extra: {
      vehicle_mode: ota.vehicle_mode ?? 'not tracked',
      ignition: ota.ignition ?? 'not tracked',
      gear: ota.gear ?? 'not tracked',
      speed: ota.speed ?? 'not tracked',
      parking_brake: ota.parking_brake ?? 'not tracked',
      battery: ota.battery ?? 'not tracked',
      network: ota.network ?? 'not tracked',
      target_system_running: ota.target_system_running ?? 'not tracked',
    },
    kill_switch: Boolean(
      summary.checks?.some((c) => c.key === 'kill_switch' && !c.passed),
    ),
    kill_reason: '',
    demo_ready: summary.ready,
    demo_ready_label: summary.ready ? 'YES' : 'NO',
    demo_ready_reasons: demoReasons,
    state_file: summary.state_file,
    snapshot: summary.snapshot,
    evaluation: summary.evaluation,
    checks: summary.checks,
    failed_checks: summary.failed_checks,
    ready: summary.ready,
    source: summary.source,
    source_label: summary.source_label,
    simulation_notice: summary.simulation_notice,
    simulation_notice_ko: summary.simulation_notice_ko,
  };
}

function registerVehicleSafetyRoutes(app, deps) {
  const { DEPLOY_ROOT, syncRuntimeCmdForCars } = deps;

  app.get('/api/vehicles/:carId/safety-state', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.query.feature);
      const summary = buildSafetySummary(DEPLOY_ROOT, carId, feature);
      if (!summary.ok) {
        res.status(404).json(summary);
        return;
      }
      res.json({
        ok: true,
        execution: false,
        ...buildLegacySafetySummary(summary),
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.post('/api/vehicles/:carId/safety-state', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.body?.feature || req.query?.feature);

      let patch = {};
      if (String(req.body?.preset || '').trim() === 'prepare_demo_ready') {
        patch = { ...PREPARE_DEMO_READY_OTA };
      } else if (req.body && typeof req.body === 'object') {
        for (const key of WRITABLE_OTA_FIELDS) {
          if (req.body[key] !== undefined) patch[key] = req.body[key];
        }
      }

      if (!Object.keys(patch).length) {
        res.status(400).json({
          ok: false,
          error:
            'no writable fields provided (use safety fields, or preset=prepare_demo_ready)',
        });
        return;
      }

      const source =
        String(req.body?.preset || '').trim() === 'prepare_demo_ready'
          ? 'manual-demo'
          : String(req.body?.source || 'manual-demo');

      if (String(req.body?.preset || '').trim() === 'prepare_demo_ready') {
        prepareDemoReady(DEPLOY_ROOT, carId, { feature, source });
      } else {
        updateSafetyState(DEPLOY_ROOT, carId, patch, source);
      }

      console.log(
        `[SAFETY] updated deploy/state/${carId}.json (${source}) fields=${Object.keys(patch).join(',')}`,
      );

      if (typeof syncRuntimeCmdForCars === 'function') {
        syncRuntimeCmdForCars([carId]);
      }

      const summary = buildSafetySummary(DEPLOY_ROOT, carId, feature);
      res.json({
        ok: true,
        execution: true,
        car_id: carId,
        updated_fields: Object.keys(patch),
        ...buildLegacySafetySummary(summary),
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });
}

module.exports = {
  registerVehicleSafetyRoutes,
  buildLegacySafetySummary,
};
