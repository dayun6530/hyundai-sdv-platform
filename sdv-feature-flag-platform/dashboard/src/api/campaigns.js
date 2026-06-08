function fetchNoStore(url, options = {}) {
  return fetch(url, {
    cache: 'no-store',
    ...options,
    headers: {
      'cache-control': 'no-cache',
      pragma: 'no-cache',
      ...(options.headers || {}),
    },
  });
}

async function fetchCampaignJson(url, options = {}) {
  try {
    const res = await fetchNoStore(url, options);
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
    return {
      ok: false,
      error: err.message || String(err),
      campaigns: [],
      runs: [],
    };
  }
}

export async function fetchCampaigns() {
  return fetchCampaignJson('/api/campaigns');
}

export async function fetchCampaignDetail(campaignId) {
  const id = encodeURIComponent(String(campaignId || '').trim());
  return fetchCampaignJson(`/api/campaigns/${id}`);
}

export async function fetchCampaignRuns({
  campaign_id,
  limit = 20,
  phase,
  status,
} = {}) {
  const q = new URLSearchParams();
  q.set('limit', String(limit));
  if (campaign_id) q.set('campaign_id', String(campaign_id));
  if (phase) q.set('phase', String(phase));
  if (status) q.set('status', String(status));
  const path = campaign_id
    ? `/api/campaigns/${encodeURIComponent(campaign_id)}/runs?${q.toString()}`
    : `/api/campaign-runs?${q.toString()}`;
  return fetchCampaignJson(path);
}

export async function fetchCampaignRun(runId) {
  const id = encodeURIComponent(String(runId || '').trim());
  return fetchCampaignJson(`/api/campaign-runs/${id}`);
}

export async function fetchVehicleRuntimeSummary(carId) {
  const id = encodeURIComponent(String(carId || '').trim());
  return fetchCampaignJson(`/api/vehicles/${id}/runtime-summary`);
}

export async function fetchRolloutPolicies() {
  return fetchCampaignJson('/api/rollout-policies');
}

/** Align deploy/version_map.json slot B (or legacy_slot) with the selected campaign. */
export async function syncCampaignVersionMap(campaignId) {
  const id = encodeURIComponent(String(campaignId || '').trim());
  return fetchCampaignJson(`/api/campaigns/${id}/sync-version-map`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: '{}',
  });
}

export async function fetchDockerLogs(carId, feature, role, tail = 60) {
  const car = encodeURIComponent(String(carId || '').trim());
  const feat = encodeURIComponent(String(feature || '').trim());
  const r = encodeURIComponent(String(role || '').trim());
  const q = new URLSearchParams({ tail: String(tail) });
  return fetchCampaignJson(
    `/api/logs/docker/${car}/${feat}/${r}?${q.toString()}`,
  );
}

function buildDeployTargetBody(target) {
  if (!target || typeof target !== 'object') return undefined;
  const vehicle_ids = (Array.isArray(target.vehicle_ids)
    ? target.vehicle_ids
    : []
  )
    .map((id) => String(id || '').trim())
    .filter(Boolean);
  if (vehicle_ids.length) return { vehicle_ids };
  const region = String(target.region || '')
    .trim()
    .toUpperCase();
  if (region) return { region };
  return undefined;
}

export async function runCampaignPhase(
  campaignId,
  phase = 'candidate',
  target = null,
) {
  const id = encodeURIComponent(String(campaignId || '').trim());
  const ph = encodeURIComponent(String(phase || 'candidate').trim());
  const body = buildDeployTargetBody(target);
  try {
    const res = await fetchNoStore(`/api/campaigns/${id}/run?phase=${ph}`, {
      method: 'POST',
      headers: body ? { 'content-type': 'application/json' } : {},
      body: body ? JSON.stringify(body) : undefined,
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return {
      ok: false,
      execution: true,
      status: 'FAIL',
      phase: phase || 'candidate',
      error: err.message || String(err),
      summary: { total: 0, success: 0, failed: 1 },
      items: [{ level: 'FAIL', message: err.message || String(err) }],
    };
  }
}

export async function dropCampaignCandidate(campaignId, target = null) {
  const id = encodeURIComponent(String(campaignId || '').trim());
  const body = buildDeployTargetBody(target);
  try {
    const res = await fetchNoStore(`/api/campaigns/${id}/drop-candidate`, {
      method: 'POST',
      headers: body ? { 'content-type': 'application/json' } : {},
      body: body ? JSON.stringify(body) : undefined,
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return {
      ok: false,
      error: err.message || String(err),
      results: [],
    };
  }
}

export async function fetchCampaignCandidateStaged(campaignId, target = null) {
  const id = encodeURIComponent(String(campaignId || '').trim());
  const body = buildDeployTargetBody(target);
  try {
    const res = await fetchNoStore(`/api/campaigns/${id}/candidate-staged`, {
      method: 'POST',
      headers: body ? { 'content-type': 'application/json' } : {},
      body: body ? JSON.stringify(body) : undefined,
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return {
      ok: false,
      ready: false,
      error: err.message || String(err),
      vehicles: [],
    };
  }
}

export async function validateCampaign(campaignId, target = null) {
  const id = encodeURIComponent(String(campaignId || '').trim());
  const body = buildDeployTargetBody(target);
  try {
    const res = await fetchNoStore(`/api/campaigns/${id}/validate`, {
      method: 'POST',
      headers: body ? { 'content-type': 'application/json' } : {},
      body: body ? JSON.stringify(body) : undefined,
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return {
      ok: false,
      status: 'FAIL',
      error: err.message || String(err),
      summary: { ok: 0, warn: 0, fail: 1 },
      items: [{ level: 'FAIL', message: err.message || String(err) }],
    };
  }
}

export async function fetchVehicleSafetyState(carId, feature = 'ADAS') {
  const id = encodeURIComponent(String(carId || '').trim());
  const q = new URLSearchParams();
  if (feature) q.set('feature', String(feature));
  const qs = q.toString();
  return fetchCampaignJson(
    `/api/vehicles/${id}/safety-state${qs ? `?${qs}` : ''}`,
  );
}

export async function updateVehicleSafetyState(carId, patch, feature = 'ADAS') {
  const id = encodeURIComponent(String(carId || '').trim());
  try {
    const res = await fetchNoStore(`/api/vehicles/${id}/safety-state`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ ...patch, feature }),
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

export async function prepareVehicleDemoReady(carId, feature = 'ADAS', action = '') {
  const { prepareFleetDemoReady } = await import('./safety.js');
  return prepareFleetDemoReady(carId, feature, action);
}

export async function prepareAllTargetsDemoReady(carIds, feature = 'ADAS', action = '') {
  const { prepareAllFleetDemoReady } = await import('./safety.js');
  return prepareAllFleetDemoReady(carIds, feature, action);
}

export async function applyPendingNativeForCars(carIds, options = {}) {
  const ids = [
    ...new Set(
      (carIds || []).map((id) => String(id || '').trim()).filter(Boolean),
    ),
  ];
  if (!ids.length) return { ok: true, results: [] };
  const feature = String(options?.feature || '').trim();
  try {
    const res = await fetchNoStore('/api/fleet/apply-pending-native', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        car_ids: ids,
        ...(feature ? { feature } : {}),
      }),
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return { ok: false, error: err.message || String(err), results: [] };
  }
}

export async function fetchStalePendingWarnings(carIds, excludeFeature) {
  const ids = [
    ...new Set(
      (carIds || []).map((id) => String(id || '').trim()).filter(Boolean),
    ),
  ];
  if (!ids.length) return { ok: true, warnings: [], count: 0 };
  try {
    const res = await fetchNoStore('/api/fleet/stale-pending-warnings', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        car_ids: ids,
        exclude_feature: String(excludeFeature || '').trim(),
      }),
    });
    const data = await res.json().catch(() => ({}));
    return res.ok
      ? { ok: true, ...data }
      : {
          ok: false,
          error: data?.error || `Request failed: ${res.status}`,
          warnings: [],
          count: 0,
        };
  } catch (err) {
    return {
      ok: false,
      error: err.message || String(err),
      warnings: [],
      count: 0,
    };
  }
}

export async function fetchFleetHistoryLogs({
  limit = 20,
  car_id,
  feature,
  action,
} = {}) {
  const q = new URLSearchParams({ limit: String(limit) });
  if (car_id) q.set('car_id', String(car_id));
  if (feature) q.set('feature', String(feature));
  if (action) q.set('action', String(action));
  return fetchCampaignJson(`/api/logs/fleet-history?${q.toString()}`);
}
