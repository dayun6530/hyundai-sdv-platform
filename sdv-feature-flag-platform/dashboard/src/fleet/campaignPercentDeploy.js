import { sortCarIds } from './campaignDeployTargets.js';

export const DEPLOY_FEATURES = ['ADAS', 'Navigation', 'EVEnergy'];

let rowIdSeq = 0;
export function newPercentRowId() {
  rowIdSeq += 1;
  return `pct_${rowIdSeq}`;
}

export function defaultPercentRows() {
  return [
    { id: newPercentRowId(), percent: 70, feature: '', version: '' },
    { id: newPercentRowId(), percent: 30, feature: '', version: '' },
  ];
}

export function normalizeFeatureKey(feature) {
  const s = String(feature || '')
    .trim()
    .toLowerCase();
  if (s === 'adas') return 'ADAS';
  if (s === 'navigation') return 'Navigation';
  if (s === 'evenergy' || s === 'ev energy') return 'EVEnergy';
  return String(feature || '').trim();
}

export function featuresMatch(a, b) {
  return normalizeFeatureKey(a) === normalizeFeatureKey(b);
}

export function imageOkCampaigns(campaigns) {
  return (campaigns || []).filter((c) => c.image_available === true);
}

export function featuresWithLocalImage(campaigns) {
  const ok = imageOkCampaigns(campaigns);
  const found = new Set(ok.map((c) => normalizeFeatureKey(c.feature)));
  return DEPLOY_FEATURES.filter((f) => found.has(f));
}

export function versionsForFeature(campaigns, feature) {
  if (!feature) return [];
  const ok = imageOkCampaigns(campaigns);
  const versions = ok
    .filter((c) => featuresMatch(c.feature, feature))
    .map((c) => String(c.target_version || '').trim())
    .filter(Boolean);
  return [...new Set(versions)].sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
}

export function resolveCampaignForFeatureVersion(campaigns, feature, version) {
  const ver = String(version || '').trim();
  if (!feature || !ver) return null;
  return (
    imageOkCampaigns(campaigns).find(
      (c) => featuresMatch(c.feature, feature) && c.target_version === ver,
    ) || null
  );
}

export function enrichPercentRows(rows, campaigns) {
  return (rows || []).map((row) => {
    const campaign = resolveCampaignForFeatureVersion(
      campaigns,
      row.feature,
      row.version,
    );
    return {
      ...row,
      campaignId: campaign?.campaign_id || '',
      campaign,
    };
  });
}

export function percentSum(rows) {
  return (rows || []).reduce((s, r) => s + (Number(r.percent) || 0), 0);
}

export function isPercentConfigValid(rows, campaigns) {
  if (!Array.isArray(rows) || rows.length < 2) return false;
  const sum = percentSum(rows);
  if (Math.abs(sum - 100) > 0.01) return false;
  const enriched = enrichPercentRows(rows, campaigns);
  return enriched.every((r) => r.campaignId && r.feature && r.version);
}

function mulberry32(seed) {
  let a = Number(seed) >>> 0 || 1;
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** Deterministic shuffle — same seed yields same car order for validate/run/promote. */
export function shuffleCarIds(carIds, seed) {
  const arr = sortCarIds(carIds);
  const rand = mulberry32(seed);
  for (let i = arr.length - 1; i > 0; i -= 1) {
    const j = Math.floor(rand() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}

export function allocateCarsByPercent(carIds, rows, options = {}) {
  const random = Boolean(options.random);
  const seed = options.seed ?? 1;
  const pool = random
    ? shuffleCarIds(carIds, seed)
    : sortCarIds(carIds);
  if (!pool.length || !rows?.length) return [];

  const weights = rows.map((r) => Math.max(0, Number(r.percent) || 0));
  const total = weights.reduce((a, b) => a + b, 0) || 1;
  let start = 0;

  return rows.map((row, i) => {
    const isLast = i === rows.length - 1;
    const count = isLast
      ? pool.length - start
      : Math.floor((weights[i] / total) * pool.length);
    const slice = pool.slice(start, start + count);
    start += count;
    return { ...row, carIds: slice };
  });
}

export function buildPercentDeployKey(targetKey, rows, options = {}) {
  const parts = (rows || []).map(
    (r) => `${r.percent}:${r.feature}:${r.version}:${r.campaignId || ''}`,
  );
  const random = Boolean(options.random);
  const seed = options.seed ?? 0;
  const mode = random ? `random:${seed}` : 'sequential';
  return `${targetKey}|${mode}|${parts.join(';')}`;
}

export function aggregateValidateResults(segmentResults) {
  const items = [];
  let ok = 0;
  let warn = 0;
  let fail = 0;

  for (const seg of segmentResults) {
    const label = `${seg.feature} ${seg.version} → ${(seg.carIds || []).join(', ') || '(none)'}`;
    const res = seg.res || {};
    for (const item of res.items || []) {
      items.push({
        ...item,
        message: `[${label}] ${item.message || ''}`,
      });
    }
    ok += res.summary?.ok ?? 0;
    warn += res.summary?.warn ?? 0;
    fail += res.summary?.fail ?? 0;
  }

  return {
    ok: fail === 0,
    summary: { ok, warn, fail },
    items,
    segments: segmentResults,
  };
}
