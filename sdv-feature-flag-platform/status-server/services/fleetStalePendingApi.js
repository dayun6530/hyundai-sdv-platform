const fs = require('fs');
const path = require('path');

const FLEET_FEATURES = ['ADAS', 'Navigation', 'Energy'];

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

function safeReadJson(fp) {
  try {
    return JSON.parse(fs.readFileSync(fp, 'utf8'));
  } catch {
    return null;
  }
}

function readVersionMapSlotVersion(versionMap, feature, slot) {
  const slotKey = String(slot || 'B')
    .trim()
    .toUpperCase();
  const row = versionMap?.features?.[feature]?.[slotKey];
  const v = row?.version;
  return typeof v === 'string' && v.trim() ? v.trim() : '';
}

function scanStalePendingWarnings(deployRoot, carIds, excludeFeature) {
  const exclude = String(excludeFeature || '').trim();
  const vmPath = path.join(deployRoot, 'version_map.json');
  const versionMap = fs.existsSync(vmPath) ? safeReadJson(vmPath) : null;
  const warnings = [];

  for (const carId of carIds || []) {
    const id = String(carId || '').trim();
    if (!/^car_(10|[1-9])$/.test(id)) continue;
    const statePath = path.join(deployRoot, 'state', `${id}.json`);
    if (!fs.existsSync(statePath)) continue;
    const state = safeReadJson(statePath);
    const features = state?.features;
    if (!features || typeof features !== 'object') continue;

    for (const feat of FLEET_FEATURES) {
      if (exclude && feat === exclude) continue;
      const row = features[feat];
      if (!row || typeof row !== 'object') continue;
      const pending = String(row.pending_active_version || '').trim();
      if (!pending) continue;

      const slot = String(row.pending_active_slot || 'B').trim();
      const expected = versionMap
        ? readVersionMapSlotVersion(versionMap, feat, slot)
        : '';
      const stale = expected ? !versionsMatch(pending, expected) : false;

      warnings.push({
        car_id: id,
        feature: feat,
        pending_version: pending,
        pending_slot: slot,
        expected_version: expected || null,
        stale,
        message: stale
          ? `${id} ${feat}: pending ${pending} does not match version_map slot ${slot} (${expected || 'unknown'})`
          : `${id} ${feat}: pending ${pending} (not part of this campaign promote)`,
      });
    }
  }

  return warnings;
}

function registerFleetStalePendingRoutes(app, deps) {
  const { DEPLOY_ROOT } = deps;

  app.post('/api/fleet/stale-pending-warnings', (req, res) => {
    const carIds = [
      ...new Set(
        (Array.isArray(req.body?.car_ids)
          ? req.body.car_ids
          : Array.isArray(req.body?.vehicle_ids)
            ? req.body.vehicle_ids
            : []
        )
          .map((id) => String(id || '').trim())
          .filter((id) => /^car_(10|[1-9])$/.test(id)),
      ),
    ];
    const excludeFeature = String(
      req.body?.exclude_feature || req.body?.campaign_feature || '',
    ).trim();

    if (!carIds.length) {
      res.status(400).json({ ok: false, error: 'car_ids required' });
      return;
    }

    const warnings = scanStalePendingWarnings(
      DEPLOY_ROOT,
      carIds,
      excludeFeature,
    );
    res.json({
      ok: true,
      exclude_feature: excludeFeature || null,
      warnings,
      count: warnings.length,
    });
  });
}

module.exports = {
  registerFleetStalePendingRoutes,
  scanStalePendingWarnings,
};
