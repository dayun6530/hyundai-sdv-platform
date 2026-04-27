/**
 * Package registry helpers (client-side).
 */

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
  if (av && av === pv) hits.push("A");
  if (bv && bv === pv) hits.push("B");
  if (!hits.length) return "Not assigned";
  return hits.join(" + ");
}

export const FEATURE_DEFAULTS = {
  ADAS: {
    version: "v0.3",
    binary: "AdasFeature_v0_3",
    description: "Improved rainy lane detection and night front-object detection",
  },
  Navigation: {
    version: "v1.2",
    binary: "NavigationFeature_v1_2",
    description: "Improved realtime route and charging station recommendation",
  },
  Energy: {
    version: "v0.7",
    binary: "EvEnergyFeature_v0_7",
    description: "Improved range prediction with weather and driving pattern factors",
  },
};
