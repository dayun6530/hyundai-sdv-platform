/**
 * Integrated Kill Switch — unifies Dashboard fleet-kill, MQTT TEST runtime kill,
 * deploy/state persistence, fleet_history, and optional MQTT runtime dispatch.
 *
 * Test flow (see docs/KILL_SWITCH_INTEGRATION.md):
 * 1. MQTT broker + status-server + dashboard/test-ui + vehicle runtime
 * 2. ADAS KILL from MQTT TEST → runtime KILL + deploy/state kill_switch=true + history
 * 3. Dashboard shows KILL ON; Clear Kill Switch → kill_switch=false + history (no auto restart)
 */
const fs = require('fs');
const path = require('path');

const CAR_ID_RE = /^car_(10|[1-9])$/;
const STATE_FEATURES = new Set(['ADAS', 'Navigation', 'Energy']);

const RUNTIME_TO_STATE = {
  AdasFeature: 'ADAS',
  NavigationFeature: 'Navigation',
  EvEnergyFeature: 'Energy',
};

const STATE_TO_RUNTIME = {
  ADAS: 'AdasFeature',
  Navigation: 'NavigationFeature',
  Energy: 'EvEnergyFeature',
};

function normalizeFeatureName(input) {
  const raw = String(input ?? '').trim();
  if (!raw) return null;
  if (STATE_FEATURES.has(raw)) return raw;
  const key = raw.replace(/[\s_-]+/g, '').toLowerCase();
  const aliases = {
    adas: 'ADAS',
    adasfeature: 'ADAS',
    navigation: 'Navigation',
    navigationfeature: 'Navigation',
    energy: 'Energy',
    ev: 'Energy',
    evenergy: 'Energy',
    evenergyfeature: 'Energy',
  };
  return aliases[key] || null;
}

function toRuntimeFeatureName(stateFeatureOrInput) {
  const stateKey = normalizeFeatureName(stateFeatureOrInput);
  if (!stateKey) return null;
  return STATE_TO_RUNTIME[stateKey] || null;
}

function toStateFeatureKey(input) {
  return normalizeFeatureName(input);
}

function safeReadJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch {
    return null;
  }
}

function appendFleetHistory(deployRoot, record) {
  const historyPath = path.join(deployRoot, 'history', 'fleet_history.jsonl');
  fs.mkdirSync(path.dirname(historyPath), { recursive: true });
  fs.appendFileSync(historyPath, `${JSON.stringify(record)}\n`, 'utf8');
  return historyPath;
}

function readDeployState(deployRoot, carId) {
  const statePath = path.join(deployRoot, 'state', `${carId}.json`);
  if (!fs.existsSync(statePath)) {
    return { ok: false, error: `missing deploy/state/${carId}.json`, statePath, state: null };
  }
  const state = safeReadJson(statePath);
  if (!state || typeof state !== 'object') {
    return { ok: false, error: `invalid deploy/state/${carId}.json`, statePath, state: null };
  }
  return { ok: true, statePath, state };
}

function writeDeployState(statePath, state) {
  fs.writeFileSync(statePath, `${JSON.stringify(state, null, 2)}\n`, 'utf8');
}

function ensureFeatureBucket(state, featureKey) {
  if (!state.features || typeof state.features !== 'object') {
    state.features = {};
  }
  if (!state.features[featureKey] || typeof state.features[featureKey] !== 'object') {
    state.features[featureKey] = {};
  }
  return state.features[featureKey];
}

function updateDeployStateKillSwitch(state, featureKey, { reason, source, runtimeMeta }) {
  const feat = ensureFeatureBucket(state, featureKey);
  const now = new Date().toISOString();
  feat.kill_switch = true;
  feat.kill_reason = reason;
  feat.kill_source = source;
  feat.killed_at = now;
  feat.last_runtime_kill = runtimeMeta || {
    requested: false,
    dispatched: false,
    ack_status: 'pending',
  };
  return feat;
}

function augmentKillMetadata(state, featureKey, { source, runtimeMeta }) {
  const feat = state.features?.[featureKey];
  if (!feat) return null;
  feat.kill_source = source;
  feat.killed_at = feat.killed_at || new Date().toISOString();
  if (runtimeMeta) {
    feat.last_runtime_kill = runtimeMeta;
  }
  return feat;
}

function clearDeployStateKillSwitch(state, featureKey, { reason, source }) {
  const feat = state.features?.[featureKey];
  if (!feat) {
    return null;
  }
  const now = new Date().toISOString();
  if (feat.kill_reason) {
    feat.previous_kill_reason = feat.kill_reason;
  }
  feat.kill_switch = false;
  feat.kill_reason = '';
  feat.clear_reason = reason;
  feat.clear_source = source;
  feat.cleared_at = now;
  feat.last_kill_clear = {
    requested: true,
    state_updated: true,
    runtime_restarted: false,
    message:
      'Kill Switch cleared in deploy state only. Runtime restart is not automatic.',
  };
  feat.last_ack = {
    action: 'kill-switch-clear',
    status: 'success',
    timestamp: now,
    source,
  };
  return feat;
}

function dispatchRuntimeKillMqtt(mqttFlagCache, { car_id, runtime_feature }) {
  const payload = `KILL:${runtime_feature}`;
  const topicPrefix =
    String(process.env.MQTT_TOPIC_PREFIX || '/sdv/vehicle').replace(/\/$/, '');
  const topic = `${topicPrefix}/${car_id}/signals/inbound`;

  if (!mqttFlagCache || typeof mqttFlagCache.publishInboundSignal !== 'function') {
    return {
      dispatched: false,
      topic,
      payload,
      error: 'MQTT publisher unavailable',
    };
  }
  return mqttFlagCache.publishInboundSignal(car_id, payload, { topicPrefix });
}

function parseSummaryJsonFromStdout(stdout) {
  const text = String(stdout || '');
  const marker = '[SUMMARY_JSON]';
  const idx = text.lastIndexOf(marker);
  if (idx === -1) return null;
  const after = text.slice(idx + marker.length).trim();
  try {
    return JSON.parse(after);
  } catch {
    return null;
  }
}

async function runFleetKillCli(deps, { car_id, feature, reason, sync_config }) {
  const { execWslBash, detectDeploymentMgrPath, toWslPath, bashSingleQuote, platformDeployRoot } =
    deps;
  const dm = detectDeploymentMgrPath();
  const repoWsl = toWslPath(platformDeployRoot);
  let cmd = `cd ${repoWsl} && ${dm.wslPath} fleet-kill vehicle ${car_id} ${feature} ${bashSingleQuote(reason)}`;
  if (sync_config) cmd += ' --sync-config';
  const r = await execWslBash(cmd);
  return {
    ok: Number(r.code) === 0,
    exit_code: r.code,
    stdout: r.stdout || '',
    stderr: r.stderr || '',
    summary_json: parseSummaryJsonFromStdout(r.stdout),
  };
}

/**
 * @param {object} deps
 * @param {object} body
 */
async function applyIntegratedKillSwitch(deps, body) {
  const {
    DEPLOY_ROOT,
    mqttFlagCache,
    execWslBash,
    detectDeploymentMgrPath,
    toWslPath,
    bashSingleQuote,
    PLATFORM_DEPLOY_ROOT,
  } = deps;

  const car_id = String(body?.car_id || body?.vehicle_id || '').trim();
  const kill = body?.kill !== false;
  const reason = String(
    body?.reason ||
      (kill ? 'manual kill switch' : 'manual clear kill switch'),
  ).trim();
  const source = String(body?.source || 'api').trim();
  const send_runtime = body?.send_runtime === true;
  const apply_fleet_kill_policy =
    body?.apply_fleet_kill_policy !== false && source === 'dashboard';
  const sync_config = body?.sync_config === true;

  if (!CAR_ID_RE.test(car_id)) {
    const err = new Error('car_id must be car_1 .. car_10');
    err.statusCode = 400;
    throw err;
  }
  if (!reason) {
    const err = new Error('reason is required');
    err.statusCode = 400;
    throw err;
  }

  const feature =
    toStateFeatureKey(body?.feature) ||
    (body?.runtime_feature ? RUNTIME_TO_STATE[String(body.runtime_feature).trim()] : null);
  if (!feature || !STATE_FEATURES.has(feature)) {
    const err = new Error('feature must be ADAS, Navigation, or Energy');
    err.statusCode = 400;
    throw err;
  }

  const runtime_feature =
    String(body?.runtime_feature || '').trim() || toRuntimeFeatureName(feature);

  let fleetKillResult = null;
  if (kill && apply_fleet_kill_policy) {
    fleetKillResult = await runFleetKillCli(
      {
        execWslBash,
        detectDeploymentMgrPath,
        toWslPath,
        bashSingleQuote,
        platformDeployRoot: PLATFORM_DEPLOY_ROOT,
      },
      { car_id, feature, reason, sync_config },
    );
    if (!fleetKillResult.ok) {
      const err = new Error(
        fleetKillResult.summary_json?.reason ||
          fleetKillResult.stderr ||
          'fleet-kill failed',
      );
      err.statusCode = 502;
      err.details = fleetKillResult;
      throw err;
    }
  }

  const loaded = readDeployState(DEPLOY_ROOT, car_id);
  if (!loaded.ok) {
    const err = new Error(loaded.error);
    err.statusCode = 404;
    throw err;
  }

  let runtimeMeta = {
    requested: send_runtime,
    dispatched: false,
    topic: '',
    payload: `KILL:${runtime_feature}`,
    ack_status: send_runtime ? 'pending' : 'skipped',
  };

  if (kill) {
    if (apply_fleet_kill_policy) {
      augmentKillMetadata(loaded.state, feature, { source, runtimeMeta });
    } else {
      updateDeployStateKillSwitch(loaded.state, feature, {
        reason,
        source,
        runtimeMeta,
      });
    }
  } else {
    clearDeployStateKillSwitch(loaded.state, feature, { reason, source });
  }

  writeDeployState(loaded.statePath, loaded.state);

  let runtime_dispatched = false;
  let runtime_error = null;
  if (kill && send_runtime) {
    const dispatch = dispatchRuntimeKillMqtt(mqttFlagCache, {
      car_id,
      runtime_feature,
    });
    runtime_dispatched = Boolean(dispatch.dispatched);
    runtime_error = dispatch.error || null;
    runtimeMeta = {
      requested: true,
      dispatched: runtime_dispatched,
      topic: dispatch.topic,
      payload: dispatch.payload,
      ack_status: runtime_dispatched ? 'pending' : 'failed',
      error: runtime_error,
    };
    augmentKillMetadata(loaded.state, feature, { source, runtimeMeta });
    writeDeployState(loaded.statePath, loaded.state);
  }

  const historyAction = kill ? 'kill-switch' : 'kill-switch-clear';
  const historyRecord = {
    timestamp: new Date().toISOString(),
    action: historyAction,
    scope: 'vehicle',
    car_id,
    feature,
    runtime_feature,
    kill_switch: kill,
    reason,
    source,
    state_updated: true,
    runtime_dispatched,
    result: 'success',
  };
  if (runtime_error) {
    historyRecord.runtime_error = runtime_error;
  }
  if (fleetKillResult?.summary_json) {
    historyRecord.fleet_kill_summary = fleetKillResult.summary_json;
  }
  appendFleetHistory(DEPLOY_ROOT, historyRecord);

  const feat = loaded.state.features?.[feature] || {};

  return {
    ok: true,
    car_id,
    feature,
    runtime_feature,
    state_updated: true,
    runtime_dispatched,
    runtime_error,
    kill_switch: Boolean(feat.kill_switch),
    reason,
    source,
    history_written: true,
    history_action: historyAction,
    fleet_kill: fleetKillResult
      ? {
          ok: fleetKillResult.ok,
          exit_code: fleetKillResult.exit_code,
          stdout: fleetKillResult.stdout,
          stderr: fleetKillResult.stderr,
          summary_json: fleetKillResult.summary_json,
        }
      : null,
    feature_state: {
      kill_switch: feat.kill_switch,
      kill_reason: feat.kill_reason || '',
      kill_source: feat.kill_source || '',
      killed_at: feat.killed_at || '',
      cleared_at: feat.cleared_at || '',
      clear_reason: feat.clear_reason || '',
      clear_source: feat.clear_source || '',
      last_runtime_kill: feat.last_runtime_kill || null,
      last_kill_clear: feat.last_kill_clear || null,
      last_ack: feat.last_ack || null,
    },
  };
}

function registerIntegratedKillSwitchRoutes(app, deps) {
  app.post('/api/fleet/kill-switch', async (req, res) => {
    try {
      const result = await applyIntegratedKillSwitch(deps, req.body || {});
      res.json(result);
    } catch (err) {
      res.status(err.statusCode || 500).json({
        ok: false,
        error: err.message || String(err),
        details: err.details || undefined,
      });
    }
  });
}

module.exports = {
  CAR_ID_RE,
  STATE_FEATURES,
  RUNTIME_TO_STATE,
  STATE_TO_RUNTIME,
  normalizeFeatureName,
  toRuntimeFeatureName,
  toStateFeatureKey,
  updateDeployStateKillSwitch,
  clearDeployStateKillSwitch,
  augmentKillMetadata,
  appendFleetHistory,
  dispatchRuntimeKillMqtt,
  applyIntegratedKillSwitch,
  registerIntegratedKillSwitchRoutes,
};
