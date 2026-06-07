function fetchNoStore(url, options = {}) {
  return fetch(url, {
    cache: "no-store",
    ...options,
    headers: {
      "cache-control": "no-cache",
      pragma: "no-cache",
      "content-type": "application/json",
      ...(options.headers || {}),
    },
  });
}

export async function previewRollback({ car_id, feature, target_version }) {
  const res = await fetchNoStore("/api/fleet/rollback/preview", {
    method: "POST",
    body: JSON.stringify({
      car_id,
      feature,
      ...(target_version ? { target_version } : {}),
    }),
  });
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || data?.message || `Preview failed: ${res.status}`);
  }
  return data;
}

export async function applyRollback({
  car_id,
  feature,
  target_version,
  sync_config,
  reason,
}) {
  const res = await fetchNoStore("/api/fleet/rollback/apply", {
    method: "POST",
    body: JSON.stringify({
      car_id,
      feature,
      ...(target_version ? { target_version } : {}),
      sync_config: sync_config === true,
      reason: reason || "manual rollback from dashboard",
      source: "dashboard",
    }),
  });
  const data = await res.json().catch(() => null);
  if (!data) {
    throw new Error(`Apply rollback failed: ${res.status}`);
  }
  return data;
}

export async function verifyRollback({ car_id, feature, expected_primary }) {
  const res = await fetchNoStore("/api/fleet/rollback/verify", {
    method: "POST",
    body: JSON.stringify({
      car_id,
      feature,
      ...(expected_primary ? { expected_primary } : {}),
    }),
  });
  const data = await res.json().catch(() => null);
  if (!res.ok) {
    throw new Error(data?.error || `Verify failed: ${res.status}`);
  }
  return data;
}

/** Backward-compatible wrapper used by legacy fleet action callers. */
export async function applyRollbackViaFleetAction(payload) {
  const res = await fetchNoStore("/api/fleet/action", {
    method: "POST",
    body: JSON.stringify({ action: "rollback", ...payload }),
  });
  const data = await res.json().catch(() => null);
  if (!data) {
    throw new Error(`Fleet rollback failed: ${res.status}`);
  }
  return data;
}
