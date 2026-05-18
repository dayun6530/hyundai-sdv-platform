function fetchNoStore(url, options = {}) {
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

async function fetchCampaignJson(url) {
  try {
    const res = await fetchNoStore(url);
    const data = await res.json().catch(() => ({}));
    if (!res.ok) {
      return {
        ok: false,
        error: data?.error || `Request failed: ${res.status}`,
        status: res.status,
        ...data,
      };
    }
    return { ok: true, ...data };
  } catch (err) {
    return { ok: false, error: err.message || String(err), campaigns: [], runs: [] };
  }
}

export async function fetchCampaigns() {
  return fetchCampaignJson("/api/campaigns");
}

export async function fetchCampaignDetail(campaignId) {
  const id = encodeURIComponent(String(campaignId || "").trim());
  return fetchCampaignJson(`/api/campaigns/${id}`);
}

export async function fetchCampaignRuns({ campaign_id, limit = 20, phase, status } = {}) {
  const q = new URLSearchParams();
  q.set("limit", String(limit));
  if (campaign_id) q.set("campaign_id", String(campaign_id));
  if (phase) q.set("phase", String(phase));
  if (status) q.set("status", String(status));
  const path = campaign_id
    ? `/api/campaigns/${encodeURIComponent(campaign_id)}/runs?${q.toString()}`
    : `/api/campaign-runs?${q.toString()}`;
  return fetchCampaignJson(path);
}

export async function fetchCampaignRun(runId) {
  const id = encodeURIComponent(String(runId || "").trim());
  return fetchCampaignJson(`/api/campaign-runs/${id}`);
}

export async function fetchVehicleRuntimeSummary(carId) {
  const id = encodeURIComponent(String(carId || "").trim());
  return fetchCampaignJson(`/api/vehicles/${id}/runtime-summary`);
}

export async function fetchRolloutPolicies() {
  return fetchCampaignJson("/api/rollout-policies");
}

export async function fetchDockerLogs(carId, feature, role, tail = 60) {
  const car = encodeURIComponent(String(carId || "").trim());
  const feat = encodeURIComponent(String(feature || "").trim());
  const r = encodeURIComponent(String(role || "").trim());
  const q = new URLSearchParams({ tail: String(tail) });
  return fetchCampaignJson(`/api/logs/docker/${car}/${feat}/${r}?${q.toString()}`);
}

export async function runCampaignPhase(campaignId, phase = "candidate") {
  const id = encodeURIComponent(String(campaignId || "").trim());
  const ph = encodeURIComponent(String(phase || "candidate").trim());
  try {
    const res = await fetchNoStore(`/api/campaigns/${id}/run?phase=${ph}`, { method: "POST" });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return {
      ok: false,
      execution: true,
      status: "FAIL",
      phase: phase || "candidate",
      error: err.message || String(err),
      summary: { total: 0, success: 0, failed: 1 },
      items: [{ level: "FAIL", message: err.message || String(err) }],
    };
  }
}

export async function validateCampaign(campaignId) {
  const id = encodeURIComponent(String(campaignId || "").trim());
  try {
    const res = await fetchNoStore(`/api/campaigns/${id}/validate`, { method: "POST" });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return {
      ok: false,
      status: "FAIL",
      error: err.message || String(err),
      summary: { ok: 0, warn: 0, fail: 1 },
      items: [{ level: "FAIL", message: err.message || String(err) }],
    };
  }
}

export async function fetchVehicleSafetyState(carId, feature = "ADAS") {
  const id = encodeURIComponent(String(carId || "").trim());
  const q = new URLSearchParams();
  if (feature) q.set("feature", String(feature));
  const qs = q.toString();
  return fetchCampaignJson(`/api/vehicles/${id}/safety-state${qs ? `?${qs}` : ""}`);
}

export async function updateVehicleSafetyState(carId, patch, feature = "ADAS") {
  const id = encodeURIComponent(String(carId || "").trim());
  try {
    const res = await fetchNoStore(`/api/vehicles/${id}/safety-state`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ ...patch, feature }),
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

export async function prepareVehicleDemoReady(carId, feature = "ADAS") {
  const id = encodeURIComponent(String(carId || "").trim());
  try {
    const res = await fetchNoStore(`/api/vehicles/${id}/safety-state`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ preset: "prepare_demo_ready", feature }),
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

export async function fetchFleetHistoryLogs({ limit = 20, car_id, feature, action } = {}) {
  const q = new URLSearchParams({ limit: String(limit) });
  if (car_id) q.set("car_id", String(car_id));
  if (feature) q.set("feature", String(feature));
  if (action) q.set("action", String(action));
  return fetchCampaignJson(`/api/logs/fleet-history?${q.toString()}`);
}
