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
    body: JSON.stringify(body),
  });
  return parseJsonOrThrow(res);
}

export async function postVehicleRuntimeDeployShadow(body) {
  const res = await fetch("/api/vehicle-runtime/deploy-shadow", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  return parseJsonOrThrow(res);
}

export async function postVehicleRuntimePromote(body) {
  const res = await fetch("/api/vehicle-runtime/promote", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ ...body, sync_shadow: body?.sync_shadow === true }),
  });
  return parseJsonOrThrow(res);
}
