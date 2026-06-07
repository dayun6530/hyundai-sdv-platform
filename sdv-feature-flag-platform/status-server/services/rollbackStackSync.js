const fs = require('fs');
const path = require('path');

/** Maximum rollback_stack entries per feature (most-recent-first). */
const MAX_ROLLBACK_STACK_SIZE = 3;
const MAX_ROLLBACK_STACK_DEPTH = MAX_ROLLBACK_STACK_SIZE;

function normalizeVersionToken(v) {
  const s = String(v || '').trim();
  if (!s) return '';
  if (/^v/i.test(s)) return `v${s.slice(1)}`;
  if (/^\d/.test(s)) return `v${s}`;
  return s;
}

function versionKey(v) {
  const s = normalizeVersionToken(v);
  return s.replace(/^v/i, '').toLowerCase();
}

function versionsMatch(a, b) {
  const ka = versionKey(a);
  const kb = versionKey(b);
  return Boolean(ka && kb && ka === kb);
}

function nowIso() {
  return new Date().toISOString();
}

function safeReadJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch {
    return null;
  }
}

function safeWriteJson(filePath, data) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, `${JSON.stringify(data, null, 2)}\n`, 'utf8');
}

function buildRollbackStackEntry({
  slot,
  version,
  docker_image,
  reason,
  feature,
  replaced_by,
  source = 'promote',
  state_file,
  config_file,
  note,
}) {
  const ts = nowIso();
  return {
    slot: String(slot || ''),
    version: normalizeVersionToken(version),
    docker_image: String(docker_image || ''),
    image: String(docker_image || ''),
    previous_role: 'primary',
    role: 'primary',
    feature: feature ? String(feature) : '',
    replaced_by: replaced_by ? normalizeVersionToken(replaced_by) : '',
    promoted_to: replaced_by ? normalizeVersionToken(replaced_by) : '',
    recorded_at: ts,
    timestamp: ts,
    reason: reason || 'promoted_to_primary',
    source: String(source || 'promote'),
    status: 'stable',
    ...(state_file ? { state_file: String(state_file) } : {}),
    ...(config_file ? { config_file: String(config_file) } : {}),
    ...(note ? { note: String(note) } : {}),
  };
}

function pushEntryToFeatureState(featureState, entry) {
  if (!featureState || typeof featureState !== 'object') return false;
  if (!entry?.version) return false;

  const stack = Array.isArray(featureState.rollback_stack)
    ? [...featureState.rollback_stack]
    : [];
  const filtered = stack.filter(
    (e) =>
      !(
        versionsMatch(e?.version, entry.version) &&
        String(e?.docker_image || '') === String(entry.docker_image || '')
      ),
  );
  filtered.unshift(entry);
  featureState.rollback_stack = filtered.slice(0, MAX_ROLLBACK_STACK_DEPTH);
  return true;
}

/**
 * Call immediately before updating primary fields on promote (ota_vehicle / vehicle-runtime API).
 */
function pushRollbackStackBeforePromoteUpdate(
  featureState,
  newVersion,
  reason = 'promoted_to_primary',
  meta = {},
) {
  if (!featureState || typeof featureState !== 'object') return false;
  const prevVersion = featureState.primary_version || featureState.active_version;
  const newToken = normalizeVersionToken(newVersion);
  if (!prevVersion || versionsMatch(prevVersion, newToken)) {
    return false;
  }
  const entry = buildRollbackStackEntry({
    slot: featureState.primary_slot || featureState.active_slot || '',
    version: prevVersion,
    docker_image: featureState.primary_image || '',
    reason,
    feature: meta.feature || '',
    replaced_by: newToken,
    source: meta.source || 'promote',
    state_file: meta.state_file,
    config_file: meta.config_file,
    note: meta.note,
  });
  return pushEntryToFeatureState(featureState, entry);
}

function readRollbackMetadata(deployRoot, carId, feature) {
  const fp = path.join(deployRoot, 'rollback', carId, `${feature}.json`);
  if (!fs.existsSync(fp)) return null;
  return safeReadJson(fp);
}

/**
 * After Campaign / fleet-deploy promote, push previous primary from rollback metadata when stack was not updated in C++.
 */
function recordRollbackStackAfterPromote({
  deployRoot,
  carId,
  feature,
  targetVersion,
  reason = 'promoted_to_primary',
}) {
  const statePath = path.join(deployRoot, 'state', `${carId}.json`);
  if (!fs.existsSync(statePath)) {
    return { ok: false, skipped: true, reason: 'missing deploy/state file' };
  }

  const state = safeReadJson(statePath);
  const f = state?.features?.[feature];
  if (!f) {
    return { ok: false, skipped: true, reason: 'missing feature in state' };
  }

  const currentPrimary = f.primary_version || f.active_version;
  const normalizedTarget = normalizeVersionToken(
    targetVersion || currentPrimary,
  );
  if (!normalizedTarget) {
    return { ok: false, skipped: true, reason: 'missing target version' };
  }

  const meta = readRollbackMetadata(deployRoot, carId, feature);
  let prevVersion = '';
  let prevSlot = '';
  if (
    meta?.previous_version &&
    (!meta.target_version || versionsMatch(meta.target_version, normalizedTarget))
  ) {
    prevVersion = meta.previous_version;
    prevSlot = meta.previous_slot || '';
  }

  if (!prevVersion) {
    return { ok: false, skipped: true, reason: 'no previous_version in rollback metadata' };
  }

  if (versionsMatch(prevVersion, normalizedTarget)) {
    return { ok: false, skipped: true, reason: 'previous equals new primary' };
  }

  const alreadyInStack = (f.rollback_stack || []).some((e) =>
    versionsMatch(e?.version, prevVersion),
  );
  if (alreadyInStack) {
    return { ok: true, skipped: true, reason: 'already in rollback_stack' };
  }

  const entry = buildRollbackStackEntry({
    slot: prevSlot || f.primary_slot || f.active_slot || '',
    version: prevVersion,
    docker_image: f.primary_image || '',
    reason,
    feature,
    replaced_by: normalizedTarget,
    source: 'promote',
    state_file: `deploy/state/${carId}.json`,
    config_file: `config/car/${carId}.conf`,
  });
  pushEntryToFeatureState(f, entry);
  safeWriteJson(statePath, state);
  return {
    ok: true,
    pushed: entry.version,
    car_id: carId,
    feature,
    new_primary: normalizedTarget,
  };
}

module.exports = {
  MAX_ROLLBACK_STACK_SIZE,
  MAX_ROLLBACK_STACK_DEPTH,
  buildRollbackStackEntry,
  pushEntryToFeatureState,
  pushRollbackStackBeforePromoteUpdate,
  recordRollbackStackAfterPromote,
  normalizeVersionToken,
  versionsMatch,
};
