const fs = require('fs');
const path = require('path');
const {
  roleToLegacySlot,
  legacySlotToRole,
  roleToLabel,
} = require('./roleMapping');

const FEATURE_BINARY = {
  ADAS: 'AdasFeature',
  Navigation: 'NavigationFeature',
  Energy: 'EvEnergyFeature',
};

function normalizeVersion(raw) {
  const s = String(raw || '').trim();
  if (!s) return '';
  return s[0] === 'v' || s[0] === 'V' ? s : `v${s}`;
}

function slotEntryMatches(entry, next) {
  if (!entry || typeof entry !== 'object') return false;
  return (
    String(entry.version || '') === next.version &&
    String(entry.package_id || '') === next.package_id &&
    String(entry.package_path || '') === next.package_path &&
    String(entry.role || '') === next.role &&
    String(entry.binary || '') === next.binary
  );
}

/**
 * Align deploy/version_map.json slot from campaign metadata (candidate line / legacy_slot B).
 * No-op when already aligned. Does not deploy or mutate deploy/state.
 */
function syncVersionMapFromCampaign(deployRoot, campaign) {
  const feature = String(campaign?.feature || '').trim();
  if (!feature) {
    const err = new Error('campaign missing feature');
    err.statusCode = 400;
    throw err;
  }

  const targetVersion = normalizeVersion(campaign.target_version);
  if (!targetVersion) {
    const err = new Error('campaign missing target_version');
    err.statusCode = 400;
    throw err;
  }

  let slotRaw = 'B';
  if (campaign.target_role) {
    slotRaw = roleToLegacySlot(campaign.target_role) || 'B';
  } else {
    slotRaw = String(campaign.legacy_slot || campaign.internal_legacy_slot || 'B')
      .trim()
      .toUpperCase();
    if (slotRaw !== 'A' && slotRaw !== 'B') {
      slotRaw = 'B';
    }
  }
  const targetRole = legacySlotToRole(slotRaw) || 'candidate';

  const vmPath = path.join(deployRoot, 'version_map.json');
  const vmBakPath = `${vmPath}.bak`;
  if (!fs.existsSync(vmPath)) {
    const err = new Error('deploy/version_map.json not found');
    err.statusCode = 400;
    throw err;
  }

  let vm;
  try {
    vm = JSON.parse(fs.readFileSync(vmPath, 'utf8'));
  } catch (err) {
    const e = new Error(`failed to read version_map.json: ${err.message}`);
    e.statusCode = 500;
    throw e;
  }

  if (!vm.features?.[feature]?.[slotRaw]) {
    const err = new Error(`version_map missing ${feature}.${slotRaw} slot`);
    err.statusCode = 400;
    throw err;
  }

  const previous = { ...vm.features[feature][slotRaw] };
  const binary =
    String(previous.binary || '').trim() ||
    FEATURE_BINARY[feature] ||
    '';
  if (!binary) {
    const err = new Error(`cannot resolve binary for feature ${feature}`);
    err.statusCode = 400;
    throw err;
  }

  const role =
    String(campaign.target_role || previous.role || '').trim() ||
    (slotRaw === 'B' ? 'candidate' : 'stable');

  const package_id =
    String(campaign.package_id || '').trim() ||
    `${feature}_${targetVersion.replace(/^v/i, '')}`;

  const package_path =
    String(campaign.package_path || '').trim() ||
    `deploy/packages/${feature}/${targetVersion}`;

  const assigned = {
    version: targetVersion,
    role,
    binary,
    package_id,
    package_path,
  };

  if (slotEntryMatches(previous, assigned)) {
    return {
      ok: true,
      unchanged: true,
      feature,
      role: targetRole,
      role_label: roleToLabel(targetRole),
      internal_legacy_slot: slotRaw,
      slot: slotRaw,
      assigned,
      previous,
      version_map_path: 'deploy/version_map.json',
    };
  }

  vm.features[feature][slotRaw] = assigned;

  try {
    fs.copyFileSync(vmPath, vmBakPath);
  } catch (err) {
    const e = new Error(`failed to backup version_map.json: ${err.message}`);
    e.statusCode = 500;
    throw e;
  }

  try {
    fs.writeFileSync(vmPath, `${JSON.stringify(vm, null, 2)}\n`, 'utf8');
  } catch (err) {
    try {
      fs.copyFileSync(vmBakPath, vmPath);
    } catch (_) {
      /* best effort restore */
    }
    const e = new Error(`failed to write version_map.json: ${err.message}`);
    e.statusCode = 500;
    throw e;
  }

  return {
    ok: true,
    unchanged: false,
    feature,
    role: targetRole,
    role_label: roleToLabel(targetRole),
    internal_legacy_slot: slotRaw,
    slot: slotRaw,
    previous,
    assigned,
    version_map_path: 'deploy/version_map.json',
    backup_path: 'deploy/version_map.json.bak',
    message: `${feature} ${roleToLabel(targetRole)} → ${targetVersion}`,
  };
}

function registerCampaignVersionMapSyncRoutes(app, deps) {
  const { DEPLOY_ROOT, findCampaignByCampaignId, validateId } = deps;
  const CAMPAIGNS_DIR = path.join(DEPLOY_ROOT, 'campaigns');

  app.post('/api/campaigns/:campaignId/sync-version-map', (req, res) => {
    try {
      const campaignId = validateId(req.params.campaignId, 'campaignId');
      const hit = findCampaignByCampaignId(CAMPAIGNS_DIR, campaignId);
      if (!hit) {
        res.status(404).json({
          ok: false,
          error: `campaign not found: ${campaignId}`,
        });
        return;
      }

      const result = syncVersionMapFromCampaign(DEPLOY_ROOT, hit.data);
      res.json({ campaign_id: campaignId, ...result });
    } catch (err) {
      res.status(err.statusCode || 500).json({
        ok: false,
        error: err.message || String(err),
      });
    }
  });
}

module.exports = {
  syncVersionMapFromCampaign,
  registerCampaignVersionMapSyncRoutes,
  normalizeVersion,
};
