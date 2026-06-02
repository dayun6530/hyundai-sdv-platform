import { findLatestSuccessfulPromoteRun } from './campaignUiModel.js';

export const ALL_CAR_IDS = Array.from({ length: 10 }, (_, i) => `car_${i + 1}`);

export const FLEET_REGIONS = ['KR', 'US', 'EU'];

export const FLEET_MODELS = ['IONIQ5', 'IONIQ6', 'KONA_EV'];

/** Staged rollout order: canary → beta → stable */
export const FLEET_ROLLOUT_GROUPS = ['canary', 'beta', 'stable'];

export function sortCarIds(ids) {
  return [...ids].sort((a, b) => {
    const na = Number(String(a).replace('car_', ''));
    const nb = Number(String(b).replace('car_', ''));
    return na - nb;
  });
}

function normRegion(r) {
  return String(r || '')
    .trim()
    .toUpperCase();
}

function normModel(m) {
  return String(m || '')
    .trim()
    .toUpperCase();
}

export function carsInRegion(vehicles, region) {
  const r = normRegion(region);
  return sortCarIds(
    (vehicles || [])
      .filter((v) => normRegion(v.region) === r)
      .map((v) => String(v.car_id)),
  );
}

export function carsInModel(vehicles, model) {
  const m = normModel(model);
  return sortCarIds(
    (vehicles || [])
      .filter((v) => normModel(v.model) === m)
      .map((v) => String(v.car_id)),
  );
}

export function carsInRegions(vehicles, regions) {
  const set = new Set();
  for (const region of regions || []) {
    for (const id of carsInRegion(vehicles, region)) set.add(id);
  }
  return sortCarIds([...set]);
}

export function carsInGroup(vehicles, group) {
  const g = String(group || '')
    .trim()
    .toLowerCase();
  return sortCarIds(
    (vehicles || [])
      .filter((v) => String(v.group || '').toLowerCase() === g)
      .map((v) => String(v.car_id)),
  );
}

export function carsInModels(vehicles, models) {
  const set = new Set();
  for (const model of models || []) {
    for (const id of carsInModel(vehicles, model)) set.add(id);
  }
  return sortCarIds([...set]);
}

function intersectCarIds(a, b) {
  const bs = new Set(b);
  return a.filter((id) => bs.has(id));
}

/**
 * Resolve deploy targets: explicit car list wins; else union of regions × filter by models (intersection).
 */
export function resolveDeploymentCarIds({
  vehicles,
  carIds = [],
  regions = [],
  models = [],
  group = '',
}) {
  const explicit = sortCarIds(carIds || []);
  if (explicit.length) return explicit;

  const regList = (regions || []).map(normRegion).filter(Boolean);
  const modList = (models || []).map(normModel).filter(Boolean);
  const groupKey = String(group || '')
    .trim()
    .toLowerCase();

  let ids = null;
  if (regList.length) {
    ids = carsInRegions(vehicles, regList);
  }
  if (modList.length) {
    const fromModels = carsInModels(vehicles, modList);
    ids = ids === null ? fromModels : intersectCarIds(ids, fromModels);
  }
  if (groupKey && FLEET_ROLLOUT_GROUPS.includes(groupKey)) {
    const fromGroup = carsInGroup(vehicles, groupKey);
    ids = ids === null ? fromGroup : intersectCarIds(ids, fromGroup);
  }
  return ids || [];
}

export function allCarsSelected(selectedIds) {
  const set = new Set(selectedIds || []);
  return ALL_CAR_IDS.every((id) => set.has(id));
}

export function buildRuntimeTargetBody({
  carIds,
  regions,
  models,
  group,
  vehicles,
}) {
  const resolved = resolveDeploymentCarIds({
    vehicles,
    carIds,
    regions,
    models,
    group,
  });
  if (resolved.length) return { vehicle_ids: resolved };
  return null;
}

export function formatDeployTargetLabel({
  carIds,
  regions,
  models,
  group,
  vehicles,
}) {
  const resolved = resolveDeploymentCarIds({
    vehicles,
    carIds,
    regions,
    models,
    group,
  });
  if (!resolved.length) return 'Select at deploy';

  const explicitOnly =
    (carIds || []).length &&
    !(regions || []).length &&
    !(models || []).length &&
    !group;
  if (explicitOnly) {
    if (resolved.length === 1) return resolved[0];
    return `${resolved.length} vehicles: ${resolved.join(', ')}`;
  }

  const parts = [];
  if (group) {
    parts.push(`Group ${group}`);
  }
  if ((regions || []).length) {
    parts.push(
      `${(regions || []).length > 1 ? 'Regions' : 'Region'} ${(regions || []).join(', ')}`,
    );
  }
  if ((models || []).length) {
    parts.push(
      `${(models || []).length > 1 ? 'Models' : 'Model'} ${(models || []).join(', ')}`,
    );
  }
  return `${resolved.length} vehicles: ${resolved.join(', ')}${
    parts.length ? ` (${parts.join(' · ')})` : ''
  }`;
}

/** Per-stage unlock: canary → beta → stable after each group's promote succeeds. */
export function computeRolloutGroupProgress(
  runs,
  vehicles,
  campaignId,
  targetVersion,
) {
  const progress = {};
  let previousStageComplete = true;
  for (const group of FLEET_ROLLOUT_GROUPS) {
    const carIds = carsInGroup(vehicles, group);
    const complete =
      carIds.length > 0 &&
      Boolean(
        findLatestSuccessfulPromoteRun(runs, {
          carIds,
          targetVersion,
          campaignId,
        }),
      );
    progress[group] = { complete, unlocked: previousStageComplete, carIds };
    previousStageComplete = complete;
  }
  return progress;
}

export function nextRolloutGroupAfter(group) {
  const g = String(group || '').toLowerCase();
  const idx = FLEET_ROLLOUT_GROUPS.indexOf(g);
  if (idx < 0 || idx >= FLEET_ROLLOUT_GROUPS.length - 1) return null;
  return FLEET_ROLLOUT_GROUPS[idx + 1];
}
