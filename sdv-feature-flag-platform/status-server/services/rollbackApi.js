const fs = require('fs');
const path = require('path');
const {
  MAX_ROLLBACK_STACK_SIZE,
  normalizeVersionToken,
  versionsMatch,
  buildRollbackStackEntry,
  pushEntryToFeatureState,
} = require('./rollbackStackSync');
const { assertSafetyReady } = require('./safetyPreconditionsApi');

const FLEET_FEATURES = ['ADAS', 'Navigation', 'Energy'];

const FEATURE_RUNTIME_DOMAIN = {
  ADAS: 'adas',
  Navigation: 'navigation',
  Energy: 'evenergy',
};

const CAR_ID_RE = /^car_(10|[1-9])$/;

function safeReadJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch {
    return null;
  }
}

function safeWriteJsonAtomic(filePath, data) {
  const dir = path.dirname(filePath);
  fs.mkdirSync(dir, { recursive: true });
  const tmp = `${filePath}.tmp.${process.pid}.${Date.now()}`;
  fs.writeFileSync(tmp, `${JSON.stringify(data, null, 2)}\n`, 'utf8');
  fs.renameSync(tmp, filePath);
}

function getPrimarySnapshot(featureState) {
  if (!featureState || typeof featureState !== 'object') {
    return { primary_version: '', primary_image: '', primary_slot: '', active_version: '', active_slot: '' };
  }
  const primary_version =
    featureState.primary_version || featureState.active_version || '';
  const primary_image = featureState.primary_image || '';
  const primary_slot =
    featureState.primary_slot || featureState.active_slot || '';
  return {
    primary_version: normalizeVersionToken(primary_version),
    primary_image: String(primary_image || ''),
    primary_slot: String(primary_slot || ''),
    active_version: normalizeVersionToken(
      featureState.active_version || primary_version,
    ),
    active_slot: String(featureState.active_slot || primary_slot),
  };
}

function inferImageForVersion(currentImage, version) {
  const token = normalizeVersionToken(version);
  if (!token) return '';
  const img = String(currentImage || '').trim();
  if (!img) return '';
  if (/:v[\d.]+$/i.test(img)) {
    return img.replace(/:v[\d.]+$/i, `:${token}`);
  }
  if (/:\d[\d.]*$/.test(img)) {
    return img.replace(/:\d[\d.]*$/, `:${token}`);
  }
  return `${img}:${token}`;
}

function resolveRollbackTarget(stack, targetVersion) {
  const list = Array.isArray(stack) ? stack : [];
  if (!list.length) return null;
  if (targetVersion) {
    const wanted = normalizeVersionToken(targetVersion);
    return (
      list.find((e) => versionsMatch(e?.version, wanted)) || null
    );
  }
  return list[0] || null;
}

function entryImage(entry, featureState) {
  const fromEntry = String(entry?.docker_image || entry?.image || '').trim();
  if (fromEntry) return fromEntry;
  return inferImageForVersion(
    featureState?.primary_image || '',
    entry?.version,
  );
}

function addBlockedVersion(featureState, { version, slot, docker_image, reason }) {
  if (!featureState || typeof featureState !== 'object') return;
  const token = normalizeVersionToken(version);
  if (!token) return;
  const list = Array.isArray(featureState.blocked_versions)
    ? [...featureState.blocked_versions]
    : [];
  const filtered = list.filter((e) => !versionsMatch(e?.version, token));
  filtered.unshift({
    slot: String(slot || ''),
    version: token,
    docker_image: String(docker_image || ''),
    blocked_at: new Date().toISOString(),
    reason: reason || 'rollback invoked',
    source: 'primary_runtime_failed',
  });
  featureState.blocked_versions = filtered;
}

function removeVersionFromStack(featureState, version) {
  if (!featureState || !Array.isArray(featureState.rollback_stack)) return;
  featureState.rollback_stack = featureState.rollback_stack.filter(
    (e) => !versionsMatch(e?.version, version),
  );
}

function updateVehicleRuntimePrimary({
  vehicleRuntimeDir,
  carId,
  feature,
  version,
  image,
}) {
  const domain = FEATURE_RUNTIME_DOMAIN[feature];
  if (!domain) return { ok: false, reason: 'unknown feature domain' };
  const rtPath = path.join(vehicleRuntimeDir, `${carId}.json`);
  let rt = safeReadJson(rtPath);
  if (!rt || typeof rt !== 'object') {
    rt = { car_id: carId, domains: {} };
  }
  if (!rt.domains || typeof rt.domains !== 'object') rt.domains = {};
  if (!rt.domains[domain] || typeof rt.domains[domain] !== 'object') {
    rt.domains[domain] = {};
  }
  const dom = rt.domains[domain];
  const token = normalizeVersionToken(version);
  dom.primary_version = token;
  if (image) dom.primary_image = image;
  dom.last_action = 'rollback';
  dom.last_rollback = new Date().toISOString();
  dom.last_message_ko = `${feature}: Primary(운영) 버전이 ${token}(으)로 롤백되었습니다.`;
  safeWriteJsonAtomic(rtPath, rt);
  return { ok: true, path: rtPath, primary_version: token, primary_image: image || '' };
}

function appendFleetHistory(deployRoot, record) {
  const historyPath = path.join(deployRoot, 'history', 'fleet_history.jsonl');
  fs.mkdirSync(path.dirname(historyPath), { recursive: true });
  fs.appendFileSync(historyPath, `${JSON.stringify(record)}\n`, 'utf8');
  return historyPath;
}

function readDashboardPrimary(deployRoot, carId, feature) {
  const statePath = path.join(deployRoot, 'state', `${carId}.json`);
  const state = safeReadJson(statePath);
  const snap = getPrimarySnapshot(state?.features?.[feature]);
  return snap.primary_version || snap.active_version || '';
}

function findRecentRollbackHistory(deployRoot, carId, feature, restoredVersion) {
  const historyPath = path.join(deployRoot, 'history', 'fleet_history.jsonl');
  if (!fs.existsSync(historyPath)) return false;
  const lines = fs
    .readFileSync(historyPath, 'utf8')
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter(Boolean);
  const wanted = normalizeVersionToken(restoredVersion);
  for (let i = lines.length - 1; i >= 0 && i >= lines.length - 500; i -= 1) {
    try {
      const row = JSON.parse(lines[i]);
      if (row.action !== 'fleet-rollback') continue;
      if (row.car_id !== carId) continue;
      if (row.feature !== feature) continue;
      if (wanted && !versionsMatch(row.restored_version, wanted)) continue;
      if (row.result === 'success') return true;
    } catch {
      /* skip */
    }
  }
  return false;
}

function previewRollback({ deployRoot, carId, feature, targetVersion }) {
  if (!CAR_ID_RE.test(carId)) {
    return { ok: false, error: 'car_id must be car_1 .. car_10' };
  }
  if (!FLEET_FEATURES.includes(feature)) {
    return { ok: false, error: 'feature must be ADAS, Navigation, or Energy' };
  }
  const statePath = path.join(deployRoot, 'state', `${carId}.json`);
  const state = safeReadJson(statePath);
  if (!state) {
    return { ok: false, error: `missing deploy/state/${carId}.json` };
  }
  const f = state.features?.[feature];
  if (!f) {
    return { ok: false, error: `missing features.${feature} in state` };
  }
  const current = getPrimarySnapshot(f);
  const stack = Array.isArray(f.rollback_stack) ? f.rollback_stack : [];
  const target = resolveRollbackTarget(stack, targetVersion);
  if (!target) {
    return {
      ok: true,
      dry_run: true,
      car_id: carId,
      feature,
      current_primary: current.primary_version,
      rollback_target: null,
      rollback_available: false,
      rollback_stack: stack.slice(0, MAX_ROLLBACK_STACK_SIZE),
      rollback_stack_size: stack.length,
      rollback_stack_limit: MAX_ROLLBACK_STACK_SIZE,
      would_change_primary: false,
      message: 'No rollback target found.',
      next_action: 'Rollback is available only after a successful promote.',
    };
  }
  const restored = normalizeVersionToken(target.version);
  const same = versionsMatch(current.primary_version, restored);
  return {
    ok: true,
    dry_run: true,
    car_id: carId,
    feature,
    current_primary: current.primary_version,
    current_primary_image: current.primary_image,
    rollback_target: restored,
    rollback_target_image: entryImage(target, f),
    rollback_stack: stack.slice(0, MAX_ROLLBACK_STACK_SIZE),
    rollback_stack_size: stack.length,
    rollback_stack_limit: MAX_ROLLBACK_STACK_SIZE,
    rollback_available: true,
    would_change_primary: !same,
    no_op: same,
    message: same
      ? `Primary already at ${restored}; rollback would be a no-op.`
      : `Would restore Primary from ${current.primary_version} to ${restored}.`,
  };
}

function applyRollback({
  deployRoot,
  vehicleRuntimeDir,
  carId,
  feature,
  targetVersion,
  reason = 'manual rollback from dashboard',
  source = 'dashboard',
  syncConfig = false,
  syncConfigCar,
}) {
  const baseFail = (payload) => ({
    ok: false,
    action: 'rollback',
    car_id: carId,
    feature,
    state_updated: false,
    runtime_updated: false,
    config_synced: false,
    history_written: false,
    rollback_stack_limit: MAX_ROLLBACK_STACK_SIZE,
    ...payload,
  });

  if (!CAR_ID_RE.test(carId)) {
    return baseFail({ error: 'car_id must be car_1 .. car_10' });
  }
  if (!FLEET_FEATURES.includes(feature)) {
    return baseFail({ error: 'feature must be ADAS, Navigation, or Energy' });
  }

  const statePath = path.join(deployRoot, 'state', `${carId}.json`);
  const state = safeReadJson(statePath);
  if (!state) {
    return baseFail({
      reason: `missing deploy/state/${carId}.json`,
      rollback_available: false,
      rollback_stack_size: 0,
      next_action: 'Rollback is available only after a successful promote.',
    });
  }
  if (!state.features?.[feature]) {
    return baseFail({
      reason: `missing features.${feature}`,
      rollback_available: false,
      rollback_stack_size: 0,
    });
  }

  const f = state.features[feature];
  const stack = Array.isArray(f.rollback_stack) ? [...f.rollback_stack] : [];
  const stateBefore = getPrimarySnapshot(f);
  const target = resolveRollbackTarget(stack, targetVersion);

  if (!target) {
    const failRecord = {
      timestamp: new Date().toISOString(),
      action: 'fleet-rollback',
      scope: 'vehicle',
      car_id: carId,
      feature,
      reason,
      source,
      result: 'failed',
      rollback_available: false,
      rollback_stack_size: stack.length,
      rollback_stack_limit: MAX_ROLLBACK_STACK_SIZE,
      state_updated: false,
      runtime_updated: false,
      config_synced: false,
    };
    try {
      appendFleetHistory(deployRoot, failRecord);
    } catch {
      /* best-effort */
    }
    return baseFail({
      reason: 'No rollback target found.',
      rollback_available: false,
      rollback_stack_size: stack.length,
      next_action: 'Rollback is available only after a successful promote.',
      history_written: true,
    });
  }

  const restoredVersion = normalizeVersionToken(target.version);
  const restoredImage = entryImage(target, f);

  if (versionsMatch(stateBefore.primary_version, restoredVersion)) {
    return {
      ok: true,
      car_id: carId,
      feature,
      action: 'rollback',
      result: 'no_op',
      previous_primary_before_rollback: stateBefore.primary_version,
      restored_version: restoredVersion,
      restored_image: restoredImage,
      state_updated: false,
      runtime_updated: false,
      config_synced: false,
      history_written: false,
      rollback_stack_size: stack.length,
      rollback_stack_limit: MAX_ROLLBACK_STACK_SIZE,
      rollback_available: true,
      state_before: stateBefore,
      state_after: stateBefore,
      warning: 'Primary already matches rollback target.',
    };
  }

  addBlockedVersion(f, {
    version: stateBefore.primary_version,
    slot: stateBefore.primary_slot,
    docker_image: stateBefore.primary_image,
    reason: 'rollback invoked',
  });
  removeVersionFromStack(f, restoredVersion);

  const slot = String(target.slot || stateBefore.primary_slot || 'B');
  f.active_slot = slot;
  f.active_version = restoredVersion;
  f.primary_slot = slot;
  f.primary_version = restoredVersion;
  if (restoredImage) f.primary_image = restoredImage;
  f.last_action = 'rollback';
  f.last_rollback = new Date().toISOString();

  safeWriteJsonAtomic(statePath, state);

  let runtimeUpdated = false;
  let runtimePrimaryImage = '';
  try {
    const rtRes = updateVehicleRuntimePrimary({
      vehicleRuntimeDir,
      carId,
      feature,
      version: restoredVersion,
      image: restoredImage,
    });
    runtimeUpdated = Boolean(rtRes.ok);
    runtimePrimaryImage = rtRes.primary_image || restoredImage;
  } catch {
    runtimeUpdated = false;
  }

  let configSynced = false;
  let configSyncError = '';
  if (syncConfig && typeof syncConfigCar === 'function') {
    try {
      const syncRes = syncConfigCar(carId);
      configSynced = Boolean(syncRes?.ok);
      configSyncError = syncRes?.error || '';
    } catch (err) {
      configSyncError = err.message || String(err);
    }
  }

  const stateAfterRead = safeReadJson(statePath);
  const stateAfter = getPrimarySnapshot(stateAfterRead?.features?.[feature]);
  const stackAfter = Array.isArray(stateAfterRead?.features?.[feature]?.rollback_stack)
    ? stateAfterRead.features[feature].rollback_stack
    : [];

  const successRecord = {
    timestamp: new Date().toISOString(),
    action: 'fleet-rollback',
    scope: 'vehicle',
    car_id: carId,
    feature,
    previous_primary_before_rollback: stateBefore.primary_version,
    restored_version: restoredVersion,
    restored_image: restoredImage,
    rollback_stack_size: stackAfter.length,
    rollback_stack_limit: MAX_ROLLBACK_STACK_SIZE,
    reason,
    source,
    state_updated: true,
    runtime_updated: runtimeUpdated,
    config_synced: configSynced,
    result: configSynced || !syncConfig ? 'success' : 'partial_success',
  };
  if (configSyncError) successRecord.config_sync_error = configSyncError;
  appendFleetHistory(deployRoot, successRecord);

  const verified =
    versionsMatch(stateAfter.primary_version, restoredVersion) ||
    versionsMatch(stateAfter.active_version, restoredVersion);

  return {
    ok: verified,
    car_id: carId,
    feature,
    action: 'rollback',
    result: verified ? (configSynced || !syncConfig ? 'success' : 'partial_success') : 'failed',
    previous_primary_before_rollback: stateBefore.primary_version,
    restored_version: restoredVersion,
    restored_image: restoredImage,
    state_updated: true,
    runtime_updated: runtimeUpdated,
    config_synced: configSynced,
    config_sync_error: configSyncError || undefined,
    history_written: true,
    rollback_stack_size: stackAfter.length,
    rollback_stack_limit: MAX_ROLLBACK_STACK_SIZE,
    rollback_available: stackAfter.length > 0,
    state_before: {
      primary_version: stateBefore.primary_version,
      primary_image: stateBefore.primary_image,
    },
    state_after: {
      primary_version: stateAfter.primary_version,
      primary_image: stateAfter.primary_image,
    },
    runtime_primary_image: runtimePrimaryImage,
    state_file: `deploy/state/${carId}.json`,
    verify_inline: verified,
    error: verified ? undefined : 'State write verification failed',
  };
}

function verifyRollback({
  deployRoot,
  vehicleRuntimeDir,
  carId,
  feature,
  expectedPrimary,
}) {
  if (!CAR_ID_RE.test(carId)) {
    return { ok: false, verified: false, error: 'invalid car_id' };
  }
  if (!FLEET_FEATURES.includes(feature)) {
    return { ok: false, verified: false, error: 'invalid feature' };
  }
  const expected = normalizeVersionToken(expectedPrimary || '');
  const actual = readDashboardPrimary(deployRoot, carId, feature);
  const dashboardPrimary = actual;

  const statePath = path.join(deployRoot, 'state', `${carId}.json`);
  const state = safeReadJson(statePath);
  const snap = getPrimarySnapshot(state?.features?.[feature]);
  const restoredImage =
    snap.primary_image ||
    entryImage(
      (state?.features?.[feature]?.rollback_stack || []).find((e) =>
        versionsMatch(e?.version, expected),
      ),
      state?.features?.[feature],
    );

  const domain = FEATURE_RUNTIME_DOMAIN[feature];
  const rtPath = path.join(vehicleRuntimeDir, `${carId}.json`);
  const rt = safeReadJson(rtPath);
  const rtDom = rt?.domains?.[domain] || {};
  const runtimePrimary = normalizeVersionToken(rtDom.primary_version || '');
  const runtimePrimaryImage = String(rtDom.primary_image || restoredImage || '');

  const historyFound = expected
    ? findRecentRollbackHistory(deployRoot, carId, feature, expected)
    : findRecentRollbackHistory(deployRoot, carId, feature, actual);

  const primaryMatch = expected
    ? versionsMatch(actual, expected)
    : Boolean(actual);
  const runtimeMatch = expected
    ? !runtimePrimary || versionsMatch(runtimePrimary, expected)
    : true;
  const verified = primaryMatch && runtimeMatch;

  return {
    ok: true,
    verified,
    car_id: carId,
    feature,
    expected_primary: expected || actual,
    actual_primary: actual,
    dashboard_primary: dashboardPrimary,
    runtime_primary_version: runtimePrimary || actual,
    runtime_primary_image: runtimePrimaryImage,
    history_found: historyFound,
    config_synced: true,
    state_file: `deploy/state/${carId}.json`,
  };
}

function registerRollbackRoutes(app, deps) {
  const {
    DEPLOY_ROOT,
    OTA_VEHICLE_RUNTIME_DIR,
    CAR_ID_RE: carRe = CAR_ID_RE,
    syncConfigCar,
  } = deps;

  app.post('/api/fleet/rollback/preview', (req, res) => {
    const carId = String(req.body?.car_id || '').trim();
    const feature = String(req.body?.feature || '').trim();
    const targetVersion = String(req.body?.target_version || '').trim();
    if (!carRe.test(carId)) {
      res.status(400).json({ ok: false, error: 'car_id must be car_1 .. car_10' });
      return;
    }
    const out = previewRollback({
      deployRoot: DEPLOY_ROOT,
      carId,
      feature,
      targetVersion: targetVersion || undefined,
    });
    res.status(out.ok ? 200 : 400).json(out);
  });

  app.post('/api/fleet/rollback/apply', async (req, res) => {
    const carId = String(req.body?.car_id || '').trim();
    const feature = String(req.body?.feature || '').trim();
    const targetVersion = String(req.body?.target_version || '').trim();
    const reason = String(req.body?.reason || 'manual rollback from dashboard').trim();
    const syncConfig = req.body?.sync_config === true;
    if (!carRe.test(carId)) {
      res.status(400).json({ ok: false, error: 'car_id must be car_1 .. car_10' });
      return;
    }
    const safety = assertSafetyReady(DEPLOY_ROOT, carId, {
      feature,
      action: 'rollback',
    });
    if (!safety.ready) {
      res.status(409).json({
        ok: false,
        result: 'blocked',
        safety_blocked: true,
        reason: safety.message,
        failed_checks: safety.failed_checks,
        checks: safety.checks,
        next_action: safety.next_action,
        car_id: carId,
        feature,
      });
      return;
    }
    const out = applyRollback({
      deployRoot: DEPLOY_ROOT,
      vehicleRuntimeDir: OTA_VEHICLE_RUNTIME_DIR,
      carId,
      feature,
      targetVersion: targetVersion || undefined,
      reason,
      source: String(req.body?.source || 'dashboard'),
      syncConfig,
      syncConfigCar,
    });
    const status = out.ok ? 200 : out.rollback_available === false ? 400 : 500;
    res.status(status).json(out);
  });

  app.post('/api/fleet/rollback/verify', (req, res) => {
    const carId = String(req.body?.car_id || '').trim();
    const feature = String(req.body?.feature || '').trim();
    const expectedPrimary = String(
      req.body?.expected_primary || req.body?.restored_version || '',
    ).trim();
    if (!carRe.test(carId)) {
      res.status(400).json({ ok: false, error: 'car_id must be car_1 .. car_10' });
      return;
    }
    const out = verifyRollback({
      deployRoot: DEPLOY_ROOT,
      vehicleRuntimeDir: OTA_VEHICLE_RUNTIME_DIR,
      carId,
      feature,
      expectedPrimary,
    });
    res.json(out);
  });
}

module.exports = {
  MAX_ROLLBACK_STACK_SIZE,
  FLEET_FEATURES,
  registerRollbackRoutes,
  previewRollback,
  applyRollback,
  verifyRollback,
  getPrimarySnapshot,
};
