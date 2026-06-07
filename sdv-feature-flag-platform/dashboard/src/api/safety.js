function fetchNoStore(url, options = {}) {
  return fetch(url, { ...options, cache: 'no-store' });
}

function encodeCarId(carId) {
  return encodeURIComponent(String(carId || '').trim());
}

export async function fetchFleetSafety(carId, feature = 'ADAS') {
  const id = encodeCarId(carId);
  const q = feature ? `?feature=${encodeURIComponent(feature)}` : '';
  try {
    const res = await fetchNoStore(`/api/fleet/safety/${id}${q}`);
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

export async function updateFleetSafety(carId, patch, feature = 'ADAS') {
  const id = encodeCarId(carId);
  try {
    const res = await fetchNoStore(`/api/fleet/safety/${id}/update`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ ...patch, feature, source: patch?.source || 'manual-demo' }),
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

export async function prepareFleetDemoReady(carId, feature = 'ADAS', action = '') {
  const id = encodeCarId(carId);
  try {
    const res = await fetchNoStore(`/api/fleet/safety/${id}/prepare-demo-ready`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({
        feature,
        ...(action ? { action } : {}),
      }),
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

export async function assertFleetSafetyReady(carId, action, feature = 'ADAS') {
  const id = encodeCarId(carId);
  try {
    const res = await fetchNoStore(`/api/fleet/safety/${id}/assert-ready`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ action, feature }),
    });
    const data = await res.json().catch(() => ({}));
    return { ...data, http_ok: res.ok, http_status: res.status };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

export async function prepareAllFleetDemoReady(carIds, feature = 'ADAS', action = '') {
  const ids = [...new Set((carIds || []).map((id) => String(id || '').trim()).filter(Boolean))];
  if (!ids.length) return { ok: true, prepared: [], failed: [] };
  const results = await Promise.all(
    ids.map((carId) => prepareFleetDemoReady(carId, feature, action)),
  );
  const prepared = [];
  const failed = [];
  results.forEach((res, idx) => {
    if (res.ok && res.ready !== false) prepared.push(ids[idx]);
    else failed.push({ car_id: ids[idx], error: res.error || res.message || 'prepare failed', failed_checks: res.failed_checks });
  });
  return { ok: failed.length === 0, prepared, failed };
}
