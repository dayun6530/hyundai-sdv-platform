/**
 * Client-side helpers for Campaign / Rollout preview (no API).
 */

const DEFAULT_CAR_IDS = Array.from({ length: 10 }, (_, i) => `car_${i + 1}`);

export function carIdOptions(vehicles) {
  const fromData = [...new Set((vehicles || []).map((v) => v.car_id).filter(Boolean))].sort();
  if (fromData.length) return fromData;
  return DEFAULT_CAR_IDS;
}

export function matchVehiclesForScope(vehicles, scope, opts) {
  const { carId, group, region } = opts;
  const list = vehicles || [];
  switch (scope) {
    case "vehicle":
      return list.filter((v) => v.car_id === carId);
    case "group":
      return list.filter((v) => v.group === group);
    case "region":
      return list.filter((v) => v.region === region);
    case "group_region":
      return list.filter((v) => v.group === group && v.region === region);
    default:
      return [];
  }
}

export function getVersionMapSlotEntry(raw, feature, slot) {
  const e = raw?.features?.[feature]?.[slot];
  if (!e || typeof e !== "object") return null;
  return {
    version: e.version || "",
    role: e.role || "",
    binary: e.binary || "",
  };
}

/**
 * Human-readable CLI (DeploymentMgr name only; status-server uses resolved binary path).
 */
export function buildFleetDeployCliPreview({ scope, carId, group, region, feature, slot, version, syncConfig }) {
  let mid = "";
  if (scope === "vehicle") mid = `${carId} `;
  else if (scope === "group") mid = `${group} `;
  else if (scope === "region") mid = `${region} `;
  else if (scope === "group_region") mid = `${group} ${region} `;

  let cmd = `DeploymentMgr fleet-deploy ${scope} ${mid}${feature} ${slot} ${version}`;
  if (syncConfig) cmd += " --sync-config";
  return cmd;
}
