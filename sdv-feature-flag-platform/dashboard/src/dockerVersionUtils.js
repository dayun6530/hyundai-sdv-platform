/**
 * ota_vehicle.sh builds tags as sdv/<domain>-feature:v${version}.
 * Package manifests often use semver with a leading "v" (e.g. v0.3); pass "0.3" to the script.
 */
export function normalizeOtaDockerVersionArg(version) {
  const s = String(version ?? "").trim();
  if (!s) return s;
  const c0 = s[0];
  const c1 = s[1];
  if ((c0 === "v" || c0 === "V") && c1 !== undefined && /[0-9]/.test(c1)) {
    return s.slice(1);
  }
  return s;
}

export function pkgFeatureToOtaDomain(feature) {
  const f = String(feature || "").trim();
  if (f === "ADAS") return "ADAS";
  if (f === "Navigation") return "Navigation";
  if (f === "Energy") return "EvEnergy";
  return f;
}
