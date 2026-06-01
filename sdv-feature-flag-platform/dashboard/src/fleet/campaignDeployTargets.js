export const ALL_CAR_IDS = Array.from({ length: 10 }, (_, i) => `car_${i + 1}`);

export const FLEET_REGIONS = ['KR', 'US', 'EU'];

export function sortCarIds(ids) {
  return [...ids].sort((a, b) => {
    const na = Number(String(a).replace('car_', ''));
    const nb = Number(String(b).replace('car_', ''));
    return na - nb;
  });
}

export function carsInRegion(vehicles, region) {
  const r = String(region || '').toUpperCase();
  return sortCarIds(
    (vehicles || [])
      .filter((v) => String(v.region || '').toUpperCase() === r)
      .map((v) => String(v.car_id)),
  );
}

export function allCarsSelected(selectedIds) {
  const set = new Set(selectedIds || []);
  return ALL_CAR_IDS.every((id) => set.has(id));
}

export function buildRuntimeTargetBody({ carIds, region }) {
  const r = String(region || '').trim().toUpperCase();
  if (r) return { region: r };
  const ids = sortCarIds(carIds || []);
  if (ids.length) return { vehicle_ids: ids };
  return null;
}

export function formatDeployTargetLabel({ carIds, region, vehicles }) {
  if (region) {
    const list = carsInRegion(vehicles, region);
    return `Region ${region} → ${list.join(', ') || '—'}`;
  }
  const ids = sortCarIds(carIds || []);
  if (!ids.length) return 'Select at deploy';
  if (ids.length === 1) return ids[0];
  return `${ids.length} vehicles: ${ids.join(', ')}`;
}
