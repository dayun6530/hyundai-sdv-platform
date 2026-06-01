function normalizeVersionToken(v) {
  const s = String(v || '').trim();
  if (!s) return '';
  return s[0] === 'v' || s[0] === 'V'
    ? s.slice(1).toLowerCase()
    : s.toLowerCase();
}

function versionsMatch(a, b) {
  const na = normalizeVersionToken(a);
  const nb = normalizeVersionToken(b);
  return Boolean(na && nb && na === nb);
}

/**
 * True when vehicle_runtime (or deploy/state) shows the campaign target staged on
 * the candidate lane (shadow_version), and primary is not already that version alone.
 */
function isFeatureCandidateStaged(featureRow, targetVersion) {
  if (!featureRow) {
    return {
      ready: false,
      reason: 'missing feature in runtime summary',
      candidate_version: '',
      primary_version: '',
    };
  }
  const cv = String(featureRow.candidate_version || '').trim();
  const pv = String(featureRow.primary_version || '').trim();
  if (!versionsMatch(cv, targetVersion)) {
    return {
      ready: false,
      reason: cv
        ? `candidate ${cv} does not match target ${targetVersion}`
        : 'no candidate version in vehicle_runtime',
      candidate_version: cv,
      primary_version: pv,
      shadow_enabled: Boolean(featureRow.shadow_enabled),
    };
  }
  if (pv && versionsMatch(pv, targetVersion) && !featureRow.shadow_enabled) {
    return {
      ready: false,
      reason: 'primary already at target (nothing to promote)',
      candidate_version: cv,
      primary_version: pv,
      shadow_enabled: Boolean(featureRow.shadow_enabled),
    };
  }
  return {
    ready: true,
    reason: 'candidate staged in vehicle_runtime',
    candidate_version: cv,
    primary_version: pv,
    shadow_enabled: Boolean(featureRow.shadow_enabled),
  };
}

function evaluateCandidateStaged(readRuntimeRow, carIds, feature, targetVersion) {
  const vehicles = [];
  let ready = carIds.length > 0;
  for (const carId of carIds) {
    const row = readRuntimeRow(carId, feature);
    const check = isFeatureCandidateStaged(row, targetVersion);
    vehicles.push({ car_id: carId, ...check });
    if (!check.ready) ready = false;
  }
  return {
    ready,
    vehicles,
    feature,
    target_version: targetVersion,
    vehicle_count: carIds.length,
  };
}

module.exports = {
  evaluateCandidateStaged,
  versionsMatch,
  isFeatureCandidateStaged,
};
