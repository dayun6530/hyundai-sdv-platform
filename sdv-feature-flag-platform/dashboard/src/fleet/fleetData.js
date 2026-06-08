/**
 * Fleet dashboard data helpers (normalization + merge warnings).
 * Raw payloads come from status-server `/api/fleet/*` (repo-root-relative files).
 */

const FEATURE_ORDER = ["ADAS", "Navigation", "Energy"];

/**
 * @param {unknown} payload
 * @returns {{ vehicles: any[], warnings: string[] }}
 */
export function loadVehiclesNormalized(payload) {
  const warnings = Array.isArray(payload?.warnings) ? [...payload.warnings] : [];
  const vehicles = Array.isArray(payload?.vehicles) ? payload.vehicles : [];
  if (payload?.ok === false && !warnings.length) {
    warnings.push("vehicles.json could not be loaded");
  }
  return { vehicles, warnings };
}

/**
 * @param {unknown} payload
 * @returns {{ rows: { feature: string, primary: any, candidate: any, legacySlotPrimary: string, legacySlotCandidate: string }[], raw: any, warnings: string[] }}
 */
export function loadVersionMapNormalized(payload) {
  const warnings = Array.isArray(payload?.warnings) ? [...payload.warnings] : [];
  const raw = payload?.raw ?? null;
  const features = raw?.features || {};
  const rows = FEATURE_ORDER.map((feature) => ({
    feature,
    primary: features[feature]?.A ?? null,
    candidate: features[feature]?.B ?? null,
    legacySlotPrimary: "A",
    legacySlotCandidate: "B",
    // backward compat for any code still reading slotA/slotB
    slotA: features[feature]?.A ?? null,
    slotB: features[feature]?.B ?? null,
  }));
  if (payload?.ok === false && !warnings.length) {
    warnings.push("version_map.json could not be loaded");
  }
  return { rows, raw, warnings };
}

/**
 * @param {unknown} payload
 * @returns {{ states: any[], warnings: string[] }}
 */
export function loadVehicleStatesNormalized(payload) {
  const warnings = Array.isArray(payload?.warnings) ? [...payload.warnings] : [];
  const states = Array.isArray(payload?.states) ? payload.states : [];
  if (payload?.ok === false && !warnings.length) {
    warnings.push("fleet state bundle could not be loaded");
  }
  return { states, warnings };
}

/**
 * @param {unknown} payload
 * @returns {{ records: any[], meta: { record_count: number, parsed_total?: number, skipped_lines?: number, total_lines?: number }, warnings: string[] }}
 */
export function loadFleetHistoryNormalized(payload) {
  const warnings = Array.isArray(payload?.warnings) ? [...payload.warnings] : [];
  const records = Array.isArray(payload?.records) ? payload.records : [];
  const meta = {
    record_count: Number(payload?.record_count) || records.length,
    parsed_total: payload?.parsed_total,
    skipped_lines: payload?.skipped_lines,
    total_lines: payload?.total_lines,
  };
  return { records, meta, warnings };
}

export function mergeWarnings(...groups) {
  const out = [];
  const seen = new Set();
  for (const g of groups) {
    for (const w of g || []) {
      if (!w || seen.has(w)) continue;
      seen.add(w);
      out.push(w);
    }
  }
  return out;
}

export function filterCanaryVehicles(vehicles) {
  return (vehicles || []).filter((v) => v?.group === "canary");
}

/**
 * @param {unknown} payload
 * @returns {{ packages: any[], raw: any | null, warnings: string[] }}
 */
export function loadPackageRegistryNormalized(payload) {
  const warnings = Array.isArray(payload?.warnings) ? [...payload.warnings] : [];
  const packages = Array.isArray(payload?.packages) ? payload.packages : [];
  const raw = payload?.raw ?? null;
  if (payload?.ok === false && !warnings.length) {
    warnings.push("package_registry could not be loaded");
  }
  return { packages, raw, warnings };
}
