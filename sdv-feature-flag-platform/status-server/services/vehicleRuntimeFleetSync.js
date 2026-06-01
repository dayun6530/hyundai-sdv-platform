const fs = require('fs');
const path = require('path');

const RUNTIME_DOMAIN_KEY = {
  ADAS: 'adas',
  Navigation: 'navigation',
  Energy: 'evenergy',
};

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

function fleetVersionDisplay(token) {
  const s = String(token || '').trim();
  if (!s) return '';
  if (/^v/i.test(s)) return s;
  if (/^\d/.test(s)) return `v${s}`;
  return s;
}

function safeReadJson(filePath) {
  try {
    return { ok: true, data: JSON.parse(fs.readFileSync(filePath, 'utf8')) };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

function safeWriteJson(filePath, data) {
  const dir = path.dirname(filePath);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(filePath, `${JSON.stringify(data, null, 2)}\n`, 'utf8');
}

function fleetFeatureShadowMode(shadowEnabled, activeVersion, shadowVersion) {
  if (!shadowEnabled) return 'inactive';
  const norm = (x) => normalizeVersionToken(x);
  const a = norm(activeVersion);
  const s = norm(shadowVersion);
  if (!s || a === s) return 'inactive';
  return 'candidate';
}

/**
 * Feature row from vehicle_runtime JSON only (authoritative for Promote gating).
 */
function readVehicleRuntimeFeatureRow(otaVehicleRuntimeDir, carId, feature) {
  const domainKey = RUNTIME_DOMAIN_KEY[feature];
  if (!domainKey) return null;
  const fp = path.join(otaVehicleRuntimeDir, `${carId}.json`);
  if (!fs.existsSync(fp)) return null;
  const parsed = safeReadJson(fp);
  if (!parsed.ok) return null;
  const dom = parsed.data?.domains?.[domainKey] || {};
  return {
    primary_version: String(dom.primary_version || '').trim(),
    candidate_version: String(dom.shadow_version || '').trim(),
    shadow_enabled: Boolean(dom.shadow_enabled),
    shadow_mode: String(dom.shadow_mode || '').trim(),
  };
}

/**
 * If deploy/state has a shadow candidate but vehicle_runtime JSON lags, copy lane into runtime file.
 */
function repairVehicleRuntimeFromFleetState(
  deployStateDir,
  otaVehicleRuntimeDir,
  carId,
  feature,
) {
  const domainKey = RUNTIME_DOMAIN_KEY[feature];
  if (!domainKey) {
    return { repaired: false, reason: 'unsupported feature' };
  }
  const statePath = path.join(deployStateDir, `${carId}.json`);
  const rtPath = path.join(otaVehicleRuntimeDir, `${carId}.json`);
  if (!fs.existsSync(statePath)) {
    return { repaired: false, reason: 'missing fleet state' };
  }
  const stParsed = safeReadJson(statePath);
  if (!stParsed.ok) {
    return { repaired: false, reason: stParsed.error };
  }
  const sf = stParsed.data?.features?.[feature];
  if (!sf?.shadow_enabled || !String(sf.shadow_version || '').trim()) {
    return { repaired: false, reason: 'no shadow in fleet state' };
  }

  let rt = { car_id: carId, domains: {} };
  if (fs.existsSync(rtPath)) {
    const rtParsed = safeReadJson(rtPath);
    if (rtParsed.ok) rt = rtParsed.data;
  }
  if (!rt.domains || typeof rt.domains !== 'object') rt.domains = {};
  if (!rt.domains[domainKey] || typeof rt.domains[domainKey] !== 'object') {
    rt.domains[domainKey] = {};
  }
  const dom = rt.domains[domainKey];
  const shadowVer = fleetVersionDisplay(sf.shadow_version);
  const primaryVer = fleetVersionDisplay(
    sf.primary_version || sf.active_version || dom.primary_version,
  );
  const already =
    dom.shadow_enabled &&
    versionsMatch(dom.shadow_version, shadowVer) &&
    versionsMatch(dom.primary_version, primaryVer);
  if (already) {
    return { repaired: false, reason: 'already in sync' };
  }

  dom.primary_version = primaryVer || dom.primary_version || '';
  dom.shadow_version = shadowVer;
  dom.shadow_enabled = true;
  dom.shadow_mode = fleetFeatureShadowMode(
    true,
    dom.primary_version,
    dom.shadow_version,
  );
  dom.last_action = dom.last_action || 'deploy-shadow';
  dom.last_message_ko =
    dom.last_message_ko ||
    `${feature}: Shadow(관측) 실행 버전이 ${shadowVer}(으)로 변경되었습니다. Primary(운영)는 그대로입니다.`;
  rt.car_id = carId;
  safeWriteJson(rtPath, rt);
  return {
    repaired: true,
    reason: 'synced vehicle_runtime from fleet state',
    shadow_version: shadowVer,
  };
}

function clearCandidateLaneOnCar(
  deployStateDir,
  otaVehicleRuntimeDir,
  carId,
  feature,
) {
  const domainKey = RUNTIME_DOMAIN_KEY[feature];
  if (!domainKey) {
    return { ok: false, error: 'unsupported feature' };
  }

  const statePath = path.join(deployStateDir, `${carId}.json`);
  if (fs.existsSync(statePath)) {
    const stParsed = safeReadJson(statePath);
    if (stParsed.ok && stParsed.data?.features?.[feature]) {
      const f = stParsed.data.features[feature];
      f.shadow_version = '';
      f.shadow_image = '';
      f.shadow_enabled = false;
      f.shadow_slot = '';
      f.shadow_status = 'DISABLED';
      f.shadow_validation_result = '';
      f.shadow_mode = fleetFeatureShadowMode(
        false,
        f.primary_version || f.active_version,
        '',
      );
      safeWriteJson(statePath, stParsed.data);
    }
  }

  const rtPath = path.join(otaVehicleRuntimeDir, `${carId}.json`);
  if (fs.existsSync(rtPath)) {
    const rtParsed = safeReadJson(rtPath);
    if (rtParsed.ok) {
      const rt = rtParsed.data;
      if (!rt.domains) rt.domains = {};
      if (!rt.domains[domainKey]) rt.domains[domainKey] = {};
      const dom = rt.domains[domainKey];
      dom.shadow_version = '';
      dom.shadow_enabled = false;
      dom.shadow_mode = 'inactive';
      dom.last_action = 'drop-shadow';
      dom.last_message_ko = `${feature}: Shadow(관측) 후보 버전을 제거했습니다. Primary(운영)만 유지됩니다.`;
      safeWriteJson(rtPath, rt);
    }
  }

  return { ok: true, car_id: carId, feature };
}

module.exports = {
  RUNTIME_DOMAIN_KEY,
  readVehicleRuntimeFeatureRow,
  repairVehicleRuntimeFromFleetState,
  clearCandidateLaneOnCar,
  versionsMatch,
  fleetFeatureShadowMode,
};
