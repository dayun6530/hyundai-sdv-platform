/**
 * Filter fleet_history.jsonl-derived records for a single vehicle.
 */

export function historyRecordTouchesCar(rec, carId) {
  if (!rec || !carId) return false;
  if (rec.car_id === carId) return true;
  if (Array.isArray(rec.results) && rec.results.some((r) => r && String(r.car_id) === carId)) return true;
  if (Array.isArray(rec.targets) && rec.targets.some((t) => String(t) === carId)) return true;
  if (Array.isArray(rec.success_targets) && rec.success_targets.some((t) => String(t) === carId)) return true;
  if (Array.isArray(rec.failed_targets) && rec.failed_targets.some((t) => String(t) === carId)) return true;
  return false;
}

export function filterHistoryForCar(records, carId, limit = 30) {
  const list = (records || []).filter((r) => historyRecordTouchesCar(r, carId));
  const tail = list.length > limit ? list.slice(-limit) : list;
  return tail.slice().reverse();
}

export function historyExtraCell(rec) {
  const parts = [];
  if (rec.targets?.length) parts.push(`targets: ${rec.targets.join(",")}`);
  if (rec.success_targets?.length) parts.push(`ok: ${rec.success_targets.length}`);
  if (rec.failed_targets?.length) parts.push(`fail: ${rec.failed_targets.join(",")}`);
  if (Array.isArray(rec.results)) {
    const mine = rec.results.filter((r) => r && r.car_id);
    if (mine.length) parts.push(`results: ${mine.map((r) => `${r.car_id}:${r.status || r.result || "-"}`).join("; ")}`);
  }
  if (rec.shadow_enabled !== undefined) parts.push(`shadow: ${rec.shadow_enabled}`);
  if (rec.kill_reason) parts.push(`kill: ${rec.kill_reason}`);
  if (rec.restored_version) parts.push(`restored: ${rec.restored_version}`);
  return parts.length ? parts.join(" · ") : "—";
}
