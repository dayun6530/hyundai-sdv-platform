/**
 * Primary/Candidate role mapping — legacy A/B slot compatibility layer.
 *
 * Internal JSON (version_map, deploy/state) keeps A/B keys.
 * UI/API use primary/candidate with optional internal_legacy_slot in responses.
 */

const LEGACY_SLOT_PRIMARY = 'A';
const LEGACY_SLOT_CANDIDATE = 'B';

function legacySlotToRole(slot) {
  const s = String(slot ?? '')
    .trim()
    .toUpperCase();
  if (s === 'A') return 'primary';
  if (s === 'B') return 'candidate';
  return null;
}

function roleToLegacySlot(role) {
  const r = normalizeRole(role);
  if (r === 'primary') return LEGACY_SLOT_PRIMARY;
  if (r === 'candidate') return LEGACY_SLOT_CANDIDATE;
  return null;
}

function legacySlotToLabel(slot) {
  const role = legacySlotToRole(slot);
  if (role === 'primary') return 'Primary';
  if (role === 'candidate') return 'Candidate';
  return 'Unknown';
}

function normalizeRole(input) {
  const s = String(input ?? '')
    .trim()
    .toLowerCase();
  if (s === 'a' || s === 'primary') return 'primary';
  if (s === 'b' || s === 'candidate') return 'candidate';
  return null;
}

function roleToLabel(role) {
  const r = normalizeRole(role);
  if (r === 'primary') return 'Primary';
  if (r === 'candidate') return 'Candidate';
  return 'Unknown';
}

function toInternalLegacySlotIfNeeded(roleOrSlot) {
  const asRole = normalizeRole(roleOrSlot);
  if (asRole) return roleToLegacySlot(asRole);
  const s = String(roleOrSlot ?? '')
    .trim()
    .toUpperCase();
  if (s === 'A' || s === 'B') return s;
  return null;
}

function resolveLegacySlotFromRequest(body = {}) {
  const role = normalizeRole(body.role);
  const slotRaw = String(body.slot ?? body.internal_legacy_slot ?? body.legacy_slot ?? '')
    .trim()
    .toUpperCase();

  if (role && slotRaw && legacySlotToRole(slotRaw) !== role) {
    const err = new Error(
      `role/slot mismatch: role=${role} does not match internal legacy slot ${slotRaw}`,
    );
    err.statusCode = 400;
    throw err;
  }

  if (role) {
    return {
      slot: roleToLegacySlot(role),
      role,
      role_label: roleToLabel(role),
    };
  }

  if (slotRaw === 'A' || slotRaw === 'B') {
    const mapped = legacySlotToRole(slotRaw);
    return {
      slot: slotRaw,
      role: mapped,
      role_label: roleToLabel(mapped),
    };
  }

  return { slot: null, role: null, role_label: null };
}

function addRoleViewToVersionMapEntry(entry, legacySlot) {
  if (!entry || typeof entry !== 'object') return entry;
  const role = legacySlotToRole(legacySlot);
  return {
    ...entry,
    deployment_role: role,
    role,
    role_label: roleToLabel(role),
    internal_legacy_slot: legacySlot,
  };
}

function addRoleViewToVersionMap(versionMap) {
  if (!versionMap?.features || typeof versionMap.features !== 'object') {
    return versionMap;
  }
  const features = {};
  for (const [featureName, slots] of Object.entries(versionMap.features)) {
    if (!slots || typeof slots !== 'object') {
      features[featureName] = slots;
      continue;
    }
    const primaryEntry = addRoleViewToVersionMapEntry(slots.A, 'A');
    const candidateEntry = addRoleViewToVersionMapEntry(slots.B, 'B');
    features[featureName] = {
      ...slots,
      primary: primaryEntry,
      candidate: candidateEntry,
      internal: {
        primary_legacy_slot: LEGACY_SLOT_PRIMARY,
        candidate_legacy_slot: LEGACY_SLOT_CANDIDATE,
      },
    };
  }
  return { ...versionMap, features };
}

function addRoleViewToFeatureState(featureState) {
  if (!featureState || typeof featureState !== 'object') return featureState;
  const activeSlot = featureState.active_slot ?? featureState.primary_slot;
  const shadowSlot = featureState.shadow_slot;
  const fallbackSlot = featureState.fallback_slot;
  const primarySlot = featureState.primary_slot ?? activeSlot;

  return {
    ...featureState,
    primary_role: legacySlotToRole(primarySlot) || 'primary',
    candidate_role: legacySlotToRole(shadowSlot) || (shadowSlot ? legacySlotToRole(shadowSlot) : null),
    active_role: legacySlotToRole(activeSlot),
    shadow_role: legacySlotToRole(shadowSlot),
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

function enrichFleetStateWithRoleView(state) {
  if (!state || typeof state !== 'object') return state;
  const out = { ...state };
  if (state.features && typeof state.features === 'object') {
    const features = {};
    for (const [name, feat] of Object.entries(state.features)) {
      features[name] = addRoleViewToFeatureState(feat);
    }
    out.features = features;
  }
  return out;
}

function buildUiSummary({ feature, role, version, legacySlot }) {
  const r = normalizeRole(role) || legacySlotToRole(legacySlot);
  return {
    feature: feature || '',
    role: r,
    role_label: roleToLabel(r),
    version: version || '',
    internal_legacy_slot: legacySlot || roleToLegacySlot(r),
  };
}

function translateHistoryRecordForDisplay(record) {
  if (!record || typeof record !== 'object') return record;
  const out = { ...record };
  const slotFields = [
    ['target_slot', 'target_role'],
    ['previous_fallback_slot', 'previous_fallback_role'],
    ['shadow_slot', 'shadow_role'],
    ['active_slot', 'active_role'],
    ['fallback_slot', 'fallback_role'],
  ];
  for (const [slotKey, roleKey] of slotFields) {
    if (record[slotKey] != null && record[slotKey] !== '') {
      out[roleKey] = legacySlotToRole(record[slotKey]);
      out[`${roleKey}_label`] = roleToLabel(out[roleKey]);
    }
  }
  if (record.target_role) {
    out.target_role_label = roleToLabel(record.target_role);
  }
  return out;
}

function resolveManifestDeploymentRole(manifest) {
  if (!manifest || typeof manifest !== 'object') return 'candidate';
  return (
    normalizeRole(manifest.deployment_role) ||
    normalizeRole(manifest.default_role) ||
    normalizeRole(manifest.slot_role) ||
    'candidate'
  );
}

function resolveManifestFallbackRole(manifest) {
  const rb = manifest?.rollback;
  if (!rb || typeof rb !== 'object') return 'primary';
  return (
    normalizeRole(rb.fallback_role) ||
    legacySlotToRole(rb.fallback_slot) ||
    'primary'
  );
}

function enrichManifestForWrite(manifest) {
  if (!manifest || typeof manifest !== 'object') return manifest;
  const deployment_role = resolveManifestDeploymentRole(manifest);
  const out = {
    ...manifest,
    deployment_role,
    default_role: manifest.default_role || deployment_role,
    slot_role: manifest.slot_role || deployment_role,
  };
  if (manifest.rollback && typeof manifest.rollback === 'object') {
    const fallback_role = resolveManifestFallbackRole(manifest);
    out.rollback = {
      ...manifest.rollback,
      fallback_role,
      fallback_slot:
        manifest.rollback.fallback_slot || roleToLegacySlot(fallback_role),
    };
  }
  return out;
}

function enrichCampaignForWrite(campaign) {
  if (!campaign || typeof campaign !== 'object') return campaign;
  const target_role =
    normalizeRole(campaign.target_role) ||
    legacySlotToRole(campaign.legacy_slot) ||
    legacySlotToRole(campaign.internal_legacy_slot) ||
    'candidate';
  const legacy_slot = campaign.legacy_slot || roleToLegacySlot(target_role);
  return {
    ...campaign,
    target_role,
    internal_legacy_slot: legacy_slot,
    legacy_slot,
  };
}

module.exports = {
  LEGACY_SLOT_PRIMARY,
  LEGACY_SLOT_CANDIDATE,
  legacySlotToRole,
  roleToLegacySlot,
  legacySlotToLabel,
  normalizeRole,
  roleToLabel,
  toInternalLegacySlotIfNeeded,
  resolveLegacySlotFromRequest,
  addRoleViewToVersionMapEntry,
  addRoleViewToVersionMap,
  addRoleViewToFeatureState,
  enrichFleetStateWithRoleView,
  buildUiSummary,
  translateHistoryRecordForDisplay,
  resolveManifestDeploymentRole,
  resolveManifestFallbackRole,
  enrichManifestForWrite,
  enrichCampaignForWrite,
};
