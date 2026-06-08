/**
 * Package registry helpers (client-side).
 */

import {
  DEPLOYMENT_ROLES,
  legacySlotToLabel,
  legacySlotToRole,
  roleToLabel,
} from "./roleMapping.js";

export const KNOWN_MODELS = ["IONIQ5", "IONIQ6", "KONA_EV"];
export const KNOWN_REGIONS = ["KR", "US", "EU"];

export function slotAssignmentLabel(pkg, versionMapRaw) {
  if (!pkg?.feature || !pkg?.version || !versionMapRaw?.features?.[pkg.feature]) {
    return "Not assigned";
  }
  const f = versionMapRaw.features[pkg.feature];
  const pv = String(pkg.version);
  const av = f.A?.version != null ? String(f.A.version) : "";
  const bv = f.B?.version != null ? String(f.B.version) : "";
  const hits = [];
  if (av && av === pv) hits.push("Primary");
  if (bv && bv === pv) hits.push("Candidate");
  if (!hits.length) return "Not assigned";
  return hits.join(" + ");
}

export function defaultDeploymentRoleForPackage(pkg) {
  if (pkg?.role === "stable") return "primary";
  return "candidate";
}

export { DEPLOYMENT_ROLES, legacySlotToLabel, legacySlotToRole, roleToLabel };

export const FEATURE_DEFAULTS = {
  ADAS: {
    version: "v1.0",
    binary: "AdasFeature_v1_0",
    description: "Improved rainy lane detection and night front-object detection",
  },
  Navigation: {
    version: "v1.0",
    binary: "NavigationFeature_v1_0",
    description: "Improved realtime route and charging station recommendation",
  },
  Energy: {
    version: "v1.0",
    binary: "EvEnergyFeature_v1_0",
    description: "Improved range prediction with weather and driving pattern factors",
  },
};
