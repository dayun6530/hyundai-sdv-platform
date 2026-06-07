function fetchNoStore(url, options = {}) {
  // Avoid any browser/proxy caching for rapidly-changing fleet metrics (polling UI).
  return fetch(url, {
    cache: "no-store",
    ...options,
    headers: {
      "cache-control": "no-cache",
      pragma: "no-cache",
      ...(options.headers || {}),
    },
  });
}

export async function fetchFleetVehicles() {
  const res = await fetchNoStore("/api/fleet/vehicles");
  if (!res.ok) throw new Error(`Failed to fetch fleet vehicles: ${res.status}`);
  return res.json();
}

export async function fetchFleetStates() {
  const res = await fetchNoStore("/api/fleet/state");
  if (!res.ok) throw new Error(`Failed to fetch fleet state: ${res.status}`);
  return res.json();
}

export async function fetchPackageRegistry() {
  const res = await fetchNoStore("/api/fleet/package-registry");
  if (!res.ok) throw new Error(`Failed to fetch package registry: ${res.status}`);
  return res.json();
}

export async function assignVersionMapSlot(body) {
  const res = await fetchNoStore("/api/fleet/version-map/assign", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Version map assign failed: ${res.status}`);
  }
  return data;
}

export async function registerPackage(body) {
  const res = await fetchNoStore("/api/fleet/package-register", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Package register failed: ${res.status}`);
  }
  return data;
}

export async function fetchVersionMap() {
  const res = await fetchNoStore("/api/fleet/version-map");
  if (!res.ok) throw new Error(`Failed to fetch version map: ${res.status}`);
  return res.json();
}

export async function fetchRollbackStatus(carId) {
  const q = new URLSearchParams({ car_id: String(carId || "") });
  const res = await fetchNoStore(`/api/fleet/rollback-status?${q.toString()}`);
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Failed to fetch rollback status: ${res.status}`);
  }
  return data;
}

export async function fetchFleetHistory(limit = 200) {
  const q = new URLSearchParams({ limit: String(limit) });
  const res = await fetchNoStore(`/api/fleet/fleet-history?${q.toString()}`);
  if (!res.ok) throw new Error(`Failed to fetch fleet history: ${res.status}`);
  return res.json();
}

export async function setDriverApproval(car_id, approved) {
  const res = await fetchNoStore("/api/fleet/driver-approval", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ car_id, approved }),
  });
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Failed to set driver approval: ${res.status}`);
  }
  return data;
}

export async function applyKillSwitch(body) {
  const res = await fetchNoStore("/api/fleet/kill-switch", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Kill switch request failed: ${res.status}`);
  }
  return data;
}

export async function runFleetAction(payload) {
  const res = await fetch("/api/fleet/action", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(payload),
  });
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Fleet action failed: ${res.status}`);
  }
  return data;
}

export async function previewFleetCommand(payload) {
  const res = await fetchNoStore("/api/fleet/command-preview", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(payload),
  });
  const data = await res.json().catch(() => null);
  if (!data) {
    return {
      success: false,
      error: `Command preview failed: ${res.status}`,
    };
  }
  return data;
}

export async function fleetDeploy(payload) {
  const body = { ...payload, sync_config: payload?.sync_config === true };
  const res = await fetch("/api/fleet/deploy", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Failed to deploy: ${res.status}`);
  }
  return data;
}

export function parseSummaryJsonFromStdout(stdout) {
  const text = String(stdout || "");
  const marker = "[SUMMARY_JSON]";
  const idx = text.lastIndexOf(marker);
  if (idx === -1) return null;
  const after = text.slice(idx + marker.length).trim();
  try {
    return JSON.parse(after);
  } catch {
    return null;
  }
}

