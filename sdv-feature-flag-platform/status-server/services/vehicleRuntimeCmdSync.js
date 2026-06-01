/**
 * Mirrors dashboard fleet runtime (vehicle_runtime JSON) into each car CMD window.
 * Foreground car consoles tail logs/<car>/version_events.log (see start_platform_wsl.sh).
 */
const fs = require('fs');
const path = require('path');

const CAR_ID_RE = /^car_(10|[1-9])$/;
const ALL_CAR_IDS = Array.from({ length: 10 }, (_, i) => `car_${i + 1}`);

const DOMAIN_SPECS = [
  { domainKey: 'adas', fleetLabel: 'ADAS', processName: 'AdasFeature' },
  {
    domainKey: 'navigation',
    fleetLabel: 'Navigation',
    processName: 'NavigationFeature',
  },
  {
    domainKey: 'evenergy',
    fleetLabel: 'Energy',
    processName: 'EvEnergyFeature',
  },
];

/** @type {Map<string, string>} */
const lastFingerprint = new Map();

function normalizeVersionToken(v) {
  const s = String(v || '').trim();
  if (!s) return '';
  if (/^v/i.test(s) && s.length >= 2 && /\d/.test(s[1])) {
    return `v${s.slice(1)}`;
  }
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

function readFleetFeatureVersions(deployRoot, carId) {
  const fp = path.join(deployRoot, 'state', `${carId}.json`);
  if (!fs.existsSync(fp)) return {};
  const parsed = safeReadJson(fp);
  if (!parsed.ok || !parsed.data?.features) return {};
  const out = {};
  const map = [
    ['ADAS', 'adas'],
    ['Navigation', 'navigation'],
    ['Energy', 'evenergy'],
  ];
  for (const [feat, dom] of map) {
    const f = parsed.data.features[feat];
    if (!f || typeof f !== 'object') continue;
    out[dom] = {
      primary_version: normalizeVersionToken(
        f.primary_version || f.active_version || '',
      ),
      shadow_version: normalizeVersionToken(f.shadow_version || ''),
      shadow_enabled: Boolean(f.shadow_enabled),
      primary_image: String(f.primary_image || ''),
    };
  }
  return out;
}

function buildRuntimeFingerprint(carId, runtimeDir, deployRoot) {
  const rtPath = path.join(runtimeDir, `${carId}.json`);
  const stateFallback = readFleetFeatureVersions(deployRoot, carId);
  const parts = [];

  if (fs.existsSync(rtPath)) {
    const parsed = safeReadJson(rtPath);
    if (parsed.ok && parsed.data?.domains) {
      for (const spec of DOMAIN_SPECS) {
        const dom = parsed.data.domains[spec.domainKey] || {};
        const st = stateFallback[spec.domainKey] || {};
        parts.push(
          [
            spec.domainKey,
            normalizeVersionToken(
              dom.primary_version || st.primary_version || '',
            ),
            normalizeVersionToken(
              dom.shadow_version || st.shadow_version || '',
            ),
            String(dom.last_action || ''),
            dom.shadow_enabled === true || st.shadow_enabled === true
              ? '1'
              : '0',
          ].join(':'),
        );
      }
      return parts.join('|');
    }
  }

  for (const spec of DOMAIN_SPECS) {
    const st = stateFallback[spec.domainKey] || {};
    parts.push(
      [
        spec.domainKey,
        st.primary_version || '',
        st.shadow_version || '',
        '',
        st.shadow_enabled ? '1' : '0',
      ].join(':'),
    );
  }
  return parts.join('|');
}

function formatRuntimeVersionLine(carId, spec, dom, jsonRel) {
  const pv = normalizeVersionToken(dom.primary_version) || '-';
  const sv = normalizeVersionToken(dom.shadow_version);
  let line =
    `[VERSION] car_id=${carId} process=${spec.processName} feature=${spec.fleetLabel}` +
    ` role=primary primary_version=${pv}`;
  if (sv) line += ` shadow_version=${sv}`;
  line += ` source=vehicle_runtime json=${jsonRel}`;
  return line;
}

function appendCarVersionEventLines(repoRoot, carId, lines) {
  if (!lines.length || !CAR_ID_RE.test(carId)) return;
  const carLogDir = path.join(repoRoot, 'logs', carId);
  const runSdvLog = path.join(repoRoot, 'logs', 'run_sdv', `${carId}.log`);
  const iso = new Date().toISOString();
  const payload = lines.map((l) => `[${iso}] ${l}\n`).join('');
  try {
    fs.mkdirSync(carLogDir, { recursive: true });
    fs.appendFileSync(
      path.join(carLogDir, 'version_events.log'),
      payload,
      'utf8',
    );
    fs.appendFileSync(
      path.join(carLogDir, 'version_events.primary.log'),
      payload,
      'utf8',
    );
  } catch (_) {
    /* ignore */
  }
  try {
    fs.mkdirSync(path.dirname(runSdvLog), { recursive: true });
    fs.appendFileSync(runSdvLog, payload, 'utf8');
  } catch (_) {
    /* ignore */
  }
}

/**
 * Push vehicle_runtime snapshot lines to the car CMD tail log when data changed.
 * @returns {boolean} true if lines were written
 */
function syncCarVehicleRuntimeToCmd(deps, carId, options = {}) {
  const { force = false } = options;
  const { REPO_ROOT, OTA_VEHICLE_RUNTIME_DIR, DEPLOY_ROOT } = deps;
  if (!CAR_ID_RE.test(carId)) return false;

  const fp = buildRuntimeFingerprint(
    carId,
    OTA_VEHICLE_RUNTIME_DIR,
    DEPLOY_ROOT,
  );
  if (!force && lastFingerprint.get(carId) === fp) {
    return false;
  }
  lastFingerprint.set(carId, fp);

  const rtPath = path.join(OTA_VEHICLE_RUNTIME_DIR, `${carId}.json`);
  const jsonRel = path.relative(REPO_ROOT, rtPath).replace(/\\/g, '/');
  const stateFallback = readFleetFeatureVersions(DEPLOY_ROOT, carId);

  const lines = [];
  let runtimeDomains = null;
  if (fs.existsSync(rtPath)) {
    const parsed = safeReadJson(rtPath);
    if (parsed.ok) runtimeDomains = parsed.data?.domains || {};
  }

  for (const spec of DOMAIN_SPECS) {
    const dom = {
      ...(stateFallback[spec.domainKey] || {}),
      ...(runtimeDomains?.[spec.domainKey] || {}),
    };
    lines.push(formatRuntimeVersionLine(carId, spec, dom, jsonRel));
  }

  appendCarVersionEventLines(REPO_ROOT, carId, lines);
  return true;
}

function syncCarsVehicleRuntimeToCmd(deps, carIds, options = {}) {
  const ids = (carIds && carIds.length ? carIds : ALL_CAR_IDS).filter((c) =>
    CAR_ID_RE.test(c),
  );
  let n = 0;
  for (const carId of ids) {
    if (syncCarVehicleRuntimeToCmd(deps, carId, options)) n += 1;
  }
  return n;
}

function syncAllCarsVehicleRuntimeToCmd(deps, options = {}) {
  return syncCarsVehicleRuntimeToCmd(deps, ALL_CAR_IDS, options);
}

function startVehicleRuntimeCmdPoller(deps, intervalMs = 3000) {
  const ms = Math.max(1000, Number(intervalMs) || 3000);
  syncAllCarsVehicleRuntimeToCmd(deps, { force: true });
  const timer = setInterval(() => {
    try {
      syncAllCarsVehicleRuntimeToCmd(deps, { force: false });
    } catch (err) {
      console.warn(`[vehicle-runtime-cmd-sync] poll error: ${err.message}`);
    }
  }, ms);
  if (typeof timer.unref === 'function') timer.unref();
  console.log(
    `[vehicle-runtime-cmd-sync] polling car_1..car_10 every ${ms}ms → logs/<car>/version_events.log`,
  );
  return timer;
}

function extractCarIdsFromCampaign(campaignData) {
  const sel = campaignData?.target_selector;
  if (sel?.scope === 'vehicle' && Array.isArray(sel.vehicle_ids)) {
    return sel.vehicle_ids.filter((c) => CAR_ID_RE.test(String(c).trim()));
  }
  return [];
}

function invalidateCarRuntimeCache(carId) {
  if (carId) lastFingerprint.delete(carId);
  else lastFingerprint.clear();
}

module.exports = {
  ALL_CAR_IDS,
  syncCarVehicleRuntimeToCmd,
  syncCarsVehicleRuntimeToCmd,
  syncAllCarsVehicleRuntimeToCmd,
  startVehicleRuntimeCmdPoller,
  extractCarIdsFromCampaign,
  invalidateCarRuntimeCache,
  formatRuntimeVersionLine,
};
