import { normalizeOtaDockerVersionArg } from "../dockerVersionUtils.js";

function withNormalizedDockerVersion(body) {
  if (!body || typeof body !== "object") return body;
  const v = body.version;
  if (v == null || String(v).trim() === "") return body;
  return { ...body, version: normalizeOtaDockerVersionArg(v) };
}

async function parseJsonOrThrow(res) {
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Request failed: ${res.status}`);
  }
  return data;
}

export async function fetchVehicleRuntimeHealth() {
  const res = await fetch("/api/vehicle-runtime/health");
  return parseJsonOrThrow(res);
}

export async function fetchVehicleRuntimeState(carId) {
  const res = await fetch(`/api/vehicle-runtime/state/${encodeURIComponent(carId)}`);
  return parseJsonOrThrow(res);
}

export async function fetchVehicleRuntimeDockerStatus(carId, domain) {
  const q = new URLSearchParams({ car_id: carId, domain });
  const res = await fetch(`/api/vehicle-runtime/status?${q}`);
  return parseJsonOrThrow(res);
}

export async function postVehicleRuntimeBootstrap(body) {
  const res = await fetch("/api/vehicle-runtime/bootstrap", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(withNormalizedDockerVersion(body)),
  });
  return parseJsonOrThrow(res);
}

export async function postVehicleRuntimeDeployShadow(body) {
  const res = await fetch("/api/vehicle-runtime/deploy-shadow", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(withNormalizedDockerVersion(body)),
  });
  return parseJsonOrThrow(res);
}

export async function postVehicleRuntimePromote(body) {
  const res = await fetch("/api/vehicle-runtime/promote", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(
      withNormalizedDockerVersion({ ...body, sync_shadow: body?.sync_shadow === true }),
    ),
  });
  return parseJsonOrThrow(res);
}

export async function fetchVehicleRuntimePendingQueue() {
  const res = await fetch("/api/vehicle-runtime/pending-queue");
  return parseJsonOrThrow(res);
}

export async function postVehicleRuntimeQueueOperation(body) {
  const res = await fetch("/api/vehicle-runtime/queue-operation", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(withNormalizedDockerVersion(body)),
  });
  return parseJsonOrThrow(res);
}

export async function postVehicleRuntimeApplyPending() {
  const res = await fetch("/api/vehicle-runtime/apply-pending", { method: "POST" });
  return parseJsonOrThrow(res);
}
