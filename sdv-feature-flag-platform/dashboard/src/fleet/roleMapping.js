/**
 * Client-side Primary/Candidate role mapping (mirrors status-server/services/roleMapping.js).
 */

export const LEGACY_SLOT_PRIMARY = "A";
export const LEGACY_SLOT_CANDIDATE = "B";

export function legacySlotToRole(slot) {
  const s = String(slot ?? "").trim().toUpperCase();
  if (s === "A") return "primary";
  if (s === "B") return "candidate";
  return null;
}

export function roleToLegacySlot(role) {
  const r = normalizeRole(role);
  if (r === "primary") return LEGACY_SLOT_PRIMARY;
  if (r === "candidate") return LEGACY_SLOT_CANDIDATE;
  return null;
}

export function legacySlotToLabel(slot) {
  const role = legacySlotToRole(slot);
  if (role === "primary") return "Primary";
  if (role === "candidate") return "Candidate";
  return "Unknown";
}

export function normalizeRole(input) {
  const s = String(input ?? "").trim().toLowerCase();
  if (s === "a" || s === "primary") return "primary";
  if (s === "b" || s === "candidate") return "candidate";
  return null;
}

export function roleToLabel(role) {
  const r = normalizeRole(role);
  if (r === "primary") return "Primary";
  if (r === "candidate") return "Candidate";
  return "Unknown";
}

export function toInternalLegacySlotIfNeeded(roleOrSlot) {
  const asRole = normalizeRole(roleOrSlot);
  if (asRole) return roleToLegacySlot(asRole);
  const s = String(roleOrSlot ?? "").trim().toUpperCase();
  if (s === "A" || s === "B") return s;
  return null;
}

export function addRoleViewToFeatureState(featureState) {
  if (!featureState || typeof featureState !== "object") return featureState;
  const activeSlot = featureState.active_slot ?? featureState.primary_slot;
  const shadowSlot = featureState.shadow_slot;
  const fallbackSlot = featureState.fallback_slot;
  const primarySlot = featureState.primary_slot ?? activeSlot;

  return {
    ...featureState,
    primary_role: legacySlotToRole(primarySlot) || "primary",
    shadow_role: legacySlotToRole(shadowSlot),
    active_role: legacySlotToRole(activeSlot),
    fallback_role: legacySlotToRole(fallbackSlot),
    role_labels: {
      primary: roleToLabel(legacySlotToRole(primarySlot)),
      active: roleToLabel(legacySlotToRole(activeSlot)),
      shadow: roleToLabel(legacySlotToRole(shadowSlot)),
      fallback: roleToLabel(legacySlotToRole(fallbackSlot)),
    },
    internal: {
      active_slot: activeSlot ?? null,
      primary_slot: featureState.primary_slot ?? null,
      shadow_slot: shadowSlot ?? null,
      fallback_slot: fallbackSlot ?? null,
    },
  };
}

export function versionMapRowsFromRaw(raw) {
  const features = raw?.features || {};
  return ["ADAS", "Navigation", "Energy"].map((feature) => ({
    feature,
    primary: features[feature]?.A ?? null,
    candidate: features[feature]?.B ?? null,
    legacySlotPrimary: LEGACY_SLOT_PRIMARY,
    legacySlotCandidate: LEGACY_SLOT_CANDIDATE,
  }));
}

export function translateHistoryRecordForDisplay(record) {
  if (!record || typeof record !== "object") return record;
  const out = { ...record };
  for (const [slotKey, roleKey] of [
    ["target_slot", "target_role"],
    ["previous_fallback_slot", "previous_fallback_role"],
    ["shadow_slot", "shadow_role"],
    ["active_slot", "active_role"],
    ["fallback_slot", "fallback_role"],
  ]) {
    if (record[slotKey] != null && record[slotKey] !== "") {
      out[roleKey] = legacySlotToRole(record[slotKey]);
      out[`${roleKey}_label`] = roleToLabel(out[roleKey]);
    }
  }
  return out;
}

export const DEPLOYMENT_ROLES = [
  { value: "primary", label: "Primary", legacySlot: "A" },
  { value: "candidate", label: "Candidate", legacySlot: "B" },
];
