/* global SdvMapbox */
/**
 * Mapbox Directions driving route + snap ego/demo to road centerline.
 */
window.SdvRouteSnap = (function (api) {
  let routeLngLat = [];
  let routeLocal = [];
  let cumDistM = [];
  let totalLenM = 0;
  let ready = false;
  let loading = false;
  let demoDistM = 0;
  let demoTimer = null;
  let lastExternalEgoMs = 0;
  let accessToken = '';
  let onReadyCb = null;
  let lastDistAlongM = 0;
  let driveDistM = 0;
  /** @type {'none'|'directions'|'offline'} */
  let routeSource = 'none';

  const DEMO_SPEED_MPS = 14;
  /** Teheran-ro (강남) 대략 진행 방위 — local x/y 직선과 달리 실제 도로 축에 맞춤 */
  const STREET_BEARING_DEG = 112;
  const DEMO_DT_S = 0.2;
  const LANE_HALF_WIDTH_M = 1.75;

  /** One-way road layout ahead of ego (reference: 3-lane highway). */
  const ROAD_LANE_SPEC = {
    lanesOneWay: 3,
    laneWidthM: 3.5,
  };

  /** Other vehicles: distance along route (m), lateral offset (m), speed (m/s). */
  const trafficVehicles = [
    { dist: 42, lane: 3.4, speed: 11.5 },
    { dist: 95, lane: -3.4, speed: 13.2 },
    { dist: 168, lane: 3.4, speed: 9.8 },
    { dist: 240, lane: -3.4, speed: 14.5 },
    { dist: 310, lane: 3.4, speed: 12.0 },
    { dist: 72, lane: -3.4, speed: 10.5 },
  ];

  function haversineM(a, b) {
    const R = 6371000;
    const lat1 = (a[1] * Math.PI) / 180;
    const lat2 = (b[1] * Math.PI) / 180;
    const dLat = ((b[1] - a[1]) * Math.PI) / 180;
    const dLng = ((b[0] - a[0]) * Math.PI) / 180;
    const s =
      Math.sin(dLat / 2) * Math.sin(dLat / 2) +
      Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLng / 2) * Math.sin(dLng / 2);
    return 2 * R * Math.asin(Math.min(1, Math.sqrt(s)));
  }

  function rebuildMetrics() {
    cumDistM = [0];
    totalLenM = 0;
    routeLocal = routeLngLat.map((ll) => api.lngLatToLocal(ll[0], ll[1]));
    for (let i = 1; i < routeLngLat.length; i += 1) {
      totalLenM += haversineM(routeLngLat[i - 1], routeLngLat[i]);
      cumDistM.push(totalLenM);
    }
    ready = routeLngLat.length >= 2 && totalLenM > 20;
  }

  function offsetLngLatM([lng, lat], bearingDeg, distM) {
    const latRad = (lat * Math.PI) / 180;
    const br = (bearingDeg * Math.PI) / 180;
    const dLat = (distM * Math.cos(br)) / 111320;
    const dLng = (distM * Math.sin(br)) / (111320 * Math.cos(latRad));
    return [lng + dLng, lat + dLat];
  }

  /** 경위도 기준 — 테헤란로 축 방향으로만 시드 (Mapbox가 도로망에 스냅). */
  function streetWaypointsLngLat() {
    const origin = api.mapOrigin();
    const dists = [0, 70, 140, 210, 290, 370, 450, 530];
    return dists.map((d) => offsetLngLatM(origin, STREET_BEARING_DEG, d));
  }

  function densifyLngLatLine(points) {
    const out = [];
    for (let i = 0; i < points.length - 1; i += 1) {
      const a = points[i];
      const b = points[i + 1];
      const steps = 16;
      for (let s = 0; s < steps; s += 1) {
        const t = s / steps;
        out.push([a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])]);
      }
    }
    out.push(points[points.length - 1]);
    return out;
  }

  async function fetchDrivingRoute(waypointsLngLat, token) {
    if (!token || waypointsLngLat.length < 2) {
      return false;
    }
    const coords = waypointsLngLat.map((ll) => `${ll[0]},${ll[1]}`).join(';');
    const url =
      `https://api.mapbox.com/directions/v5/mapbox/driving/${coords}` +
      `?geometries=geojson&overview=full&steps=false&access_token=${encodeURIComponent(token)}`;
    const timeoutMs = 5000;
    const res = await Promise.race([
      fetch(url),
      new Promise((_, reject) => {
        setTimeout(() => reject(new Error('directions timeout')), timeoutMs);
      }),
    ]);
    const body = await res.json();
    if (!res.ok || !body.routes || !body.routes[0]) {
      console.warn('Directions API failed', body);
      return false;
    }
    routeLngLat = body.routes[0].geometry.coordinates;
    routeSource = 'directions';
    rebuildMetrics();
    return ready;
  }

  function notifyRouteReady() {
    if (onReadyCb) {
      onReadyCb();
    }
  }

  /** 즉시 사용 가능한 오프라인 경로 (Directions 대기 없음). */
  function ensureOfflineRoute() {
    if (ready && routeLngLat.length >= 2) {
      return true;
    }
    const wps = streetWaypointsLngLat();
    routeSource = 'offline';
    routeLngLat = densifyLngLatLine(wps);
    rebuildMetrics();
    return ready;
  }

  async function loadRoute(token) {
    if (routeSource === 'directions' && ready) {
      return true;
    }
    if (loading) {
      return ready;
    }
    loading = true;
    accessToken = token;
    ensureOfflineRoute();
    try {
      const wps = streetWaypointsLngLat();
      if (token && (await fetchDrivingRoute(wps, token))) {
        notifyRouteReady();
        return true;
      }
      const origin = wps[0];
      const end = wps[wps.length - 1];
      if (token && (await fetchDrivingRoute([origin, end], token))) {
        notifyRouteReady();
        return true;
      }
      ensureOfflineRoute();
      notifyRouteReady();
      return ready;
    } catch (err) {
      console.warn('Directions fetch error', err);
      ensureOfflineRoute();
      notifyRouteReady();
      return ready;
    } finally {
      loading = false;
    }
  }

  /** @deprecated use ensureOfflineRoute — kept for C++ kick compatibility */
  function loadFallbackRoute() {
    ensureOfflineRoute();
    notifyRouteReady();
    return ready;
  }

  function getRouteSource() {
    return routeSource;
  }

  /** Clamp distance to route end — no wrap back to start (avoids loop replay). */
  function clampAlong(distM) {
    if (!ready || totalLenM < 1) {
      return 0;
    }
    return Math.max(0, Math.min(distM, totalLenM - 0.5));
  }

  function isAtRouteEnd() {
    return ready && driveDistM >= totalLenM - 1.5;
  }

  /** Stable heading from a short look-ahead (reduces vertex jitter). */
  function bearingAlongRoute(distM, lookAheadM) {
    const ahead = typeof lookAheadM === 'number' ? lookAheadM : 14;
    const p0 = interpolateAtRaw(distM);
    const p1 = interpolateAtRaw(distM + ahead);
    if (!p0) {
      return 0;
    }
    if (!p1 || (Math.abs(p0.lng - p1.lng) < 1e-9 && Math.abs(p0.lat - p1.lat) < 1e-9)) {
      return p0.bearingSeg;
    }
    return api.bearingDeg([p0.lng, p0.lat], [p1.lng, p1.lat]);
  }

  /** Position only (segment bearing); use bearingAlongRoute for camera/HUD. */
  function interpolateAtRaw(distM) {
    if (!ready) {
      return null;
    }
    const d = clampAlong(distM);

    if (d >= totalLenM - 1e-3) {
      const last = routeLngLat.length - 1;
      const bearingSeg = api.bearingDeg(routeLngLat[last - 1], routeLngLat[last]);
      const local = routeLocal[last];
      return {
        lng: routeLngLat[last][0],
        lat: routeLngLat[last][1],
        x: local[0],
        y: local[1],
        bearingSeg,
      };
    }

    let i = 1;
    while (i < cumDistM.length && cumDistM[i] < d) {
      i += 1;
    }
    const d0 = cumDistM[i - 1];
    const d1 = cumDistM[i];
    const t = d1 > d0 ? (d - d0) / (d1 - d0) : 0;
    const lng = routeLngLat[i - 1][0] + t * (routeLngLat[i][0] - routeLngLat[i - 1][0]);
    const lat = routeLngLat[i - 1][1] + t * (routeLngLat[i][1] - routeLngLat[i - 1][1]);
    const bearingSeg = api.bearingDeg(routeLngLat[i - 1], routeLngLat[i]);
    const local = api.lngLatToLocal(lng, lat);
    return { lng, lat, x: local[0], y: local[1], bearingSeg };
  }

  /** Position on route; forward only, stops at end. */
  function interpolateAt(distM) {
    const raw = interpolateAtRaw(distM);
    if (!raw) {
      return null;
    }
    const bearing = bearingAlongRoute(distM, 14);
    return {
      lng: raw.lng,
      lat: raw.lat,
      x: raw.x,
      y: raw.y,
      yawRad: api.bearingToYawRad(bearing),
      bearing,
    };
  }

  function snapLocal(x, y, allowBackward) {
    if (!ready) {
      return { x, y, yawRad: 0, bearing: 0, snapped: false };
    }
    let bestDistAlong = 0;
    let bestScore = Infinity;
    for (let i = 1; i < routeLocal.length; i += 1) {
      const ax = routeLocal[i - 1][0];
      const ay = routeLocal[i - 1][1];
      const bx = routeLocal[i][0];
      const by = routeLocal[i][1];
      const vx = bx - ax;
      const vy = by - ay;
      const len2 = vx * vx + vy * vy;
      let t = 0;
      if (len2 > 1e-6) {
        t = ((x - ax) * vx + (y - ay) * vy) / len2;
        t = Math.max(0, Math.min(1, t));
      }
      const px = ax + t * vx;
      const py = ay + t * vy;
      const d2 = (x - px) * (x - px) + (y - py) * (y - py);
      const segLen = cumDistM[i] - cumDistM[i - 1];
      const distAlong = cumDistM[i - 1] + t * segLen;
      const alongPenalty = Math.abs(distAlong - lastDistAlongM) * 0.15;
      const score = d2 + alongPenalty;
      if (score < bestScore) {
        bestScore = score;
        bestDistAlong = distAlong;
      }
    }
    if (!allowBackward && bestDistAlong + 1.0 < lastDistAlongM) {
      bestDistAlong = lastDistAlongM;
    } else {
      lastDistAlongM = bestDistAlong;
    }
    return { ...interpolateAt(bestDistAlong), snapped: true };
  }

  function routeLineFeatureLngLat() {
    return {
      type: 'Feature',
      geometry: { type: 'LineString', coordinates: routeLngLat.slice() },
    };
  }

  function aheadTrajectoryFeature(fromDistM, aheadM, points) {
    if (!ready) {
      return null;
    }
    const n = Math.max(8, points || 48);
    const coords = [];
    for (let i = 0; i <= n; i += 1) {
      const pos = interpolateAt(fromDistM + (aheadM * i) / n);
      if (pos) {
        coords.push([pos.lng, pos.lat]);
      }
    }
    return {
      type: 'Feature',
      geometry: { type: 'LineString', coordinates: coords },
    };
  }

  /** Road surface polygon (halfWidthM each side) along route segment — sim-style wide pavement. */
  function roadCorridorFeature(fromDistM, aheadM, halfWidthM, points) {
    if (!ready) {
      return null;
    }
    const halfW = typeof halfWidthM === 'number' && halfWidthM > 0 ? halfWidthM : LANE_HALF_WIDTH_M;
    const n = Math.max(12, points || 40);
    const left = [];
    const right = [];
    for (let i = 0; i <= n; i += 1) {
      const pos = interpolateAt(fromDistM + (aheadM * i) / n);
      if (!pos) {
        continue;
      }
      const nx = -Math.sin(pos.yawRad);
      const ny = Math.cos(pos.yawRad);
      const ll = api.localToLngLat(pos.x + nx * halfW, pos.y + ny * halfW);
      const rr = api.localToLngLat(pos.x - nx * halfW, pos.y - ny * halfW);
      left.push(ll);
      right.push(rr);
    }
    if (left.length < 2) {
      return null;
    }
    const ring = left.concat(right.reverse());
    ring.push(left[0]);
    return {
      type: 'Feature',
      geometry: { type: 'Polygon', coordinates: [ring] },
    };
  }

  function laneBandFeature(fromDistM, aheadM, points) {
    return roadCorridorFeature(fromDistM, aheadM, LANE_HALF_WIDTH_M, points);
  }

  function roadLaneSpec() {
    return { lanesOneWay: ROAD_LANE_SPEC.lanesOneWay, laneWidthM: ROAD_LANE_SPEC.laneWidthM };
  }

  function roadHalfWidthM(lanesOneWay, laneWidthM) {
    const lanes = lanesOneWay || ROAD_LANE_SPEC.lanesOneWay;
    const w = laneWidthM || ROAD_LANE_SPEC.laneWidthM;
    return (lanes * w) / 2;
  }

  /** Parallel line along route at lateral offset (meters, + = left of travel direction). */
  function offsetLineAlongRoute(fromDistM, aheadM, lateralOffsetM, points) {
    if (!ready) {
      return null;
    }
    const n = Math.max(16, points || 52);
    const coords = [];
    for (let i = 0; i <= n; i += 1) {
      const pos = interpolateAt(fromDistM + (aheadM * i) / n);
      if (!pos) {
        continue;
      }
      const nx = -Math.sin(pos.yawRad);
      const ny = Math.cos(pos.yawRad);
      coords.push(api.localToLngLat(pos.x + nx * lateralOffsetM, pos.y + ny * lateralOffsetM));
    }
    return coords.length >= 2 ? coords : null;
  }

  /** Lane edges (solid) + dividers (dashed) for a one-way multi-lane road. */
  function laneMarkingsFeatureCollection(fromDistM, aheadM, lanesOneWay, laneWidthM) {
    const lanes = Math.max(1, Math.round(lanesOneWay || ROAD_LANE_SPEC.lanesOneWay));
    const w = laneWidthM || ROAD_LANE_SPEC.laneWidthM;
    const half = (lanes * w) / 2;
    const features = [];
    const edgeL = offsetLineAlongRoute(fromDistM, aheadM, -half, 56);
    const edgeR = offsetLineAlongRoute(fromDistM, aheadM, half, 56);
    if (edgeL) {
      features.push({
        type: 'Feature',
        properties: { kind: 'edge' },
        geometry: { type: 'LineString', coordinates: edgeL },
      });
    }
    if (edgeR) {
      features.push({
        type: 'Feature',
        properties: { kind: 'edge' },
        geometry: { type: 'LineString', coordinates: edgeR },
      });
    }
    for (let i = 1; i < lanes; i += 1) {
      const off = -half + i * w;
      const dash = offsetLineAlongRoute(fromDistM, aheadM, off, 56);
      if (dash) {
        features.push({
          type: 'Feature',
          properties: { kind: 'dash' },
          geometry: { type: 'LineString', coordinates: dash },
        });
      }
    }
    return { type: 'FeatureCollection', features };
  }

  function markExternalEgo() {
    lastExternalEgoMs = Date.now();
  }

  function externalEgoActive() {
    return Date.now() - lastExternalEgoMs < 600;
  }

  /** Cockpit drive is advanced from C++ via setDriveDistance — no duplicate JS timer. */
  function startDemo(onTick) {
    stopDemo();
    demoDistM = 0;
    driveDistM = 0;
    lastDistAlongM = 0;
    if (onTick) {
      const pos = interpolateAt(0);
      if (pos) {
        onTick(pos, 0, true);
      }
    }
  }

  function setDriveDistance(distM) {
    if (!ready) {
      return;
    }
    const d = clampAlong(distM);
    driveDistM = d;
    demoDistM = d;
    lastDistAlongM = d;
  }

  function getDriveDistance() {
    return driveDistM;
  }

  function tickTraffic(dtS) {
    if (!ready || totalLenM < 1) {
      return;
    }
    trafficVehicles.forEach((v) => {
      v.dist = clampAlong(v.dist + v.speed * dtS);
    });
  }

  function stopDemo() {
    if (demoTimer) {
      clearInterval(demoTimer);
      demoTimer = null;
    }
  }

  function isReady() {
    return ready;
  }

  function onReady(cb) {
    onReadyCb = cb;
  }

  function nearestDistanceAlongRoute(x, y) {
    const s = snapLocal(x, y);
    if (!s.snapped) {
      return 0;
    }
    for (let i = 1; i < routeLocal.length; i += 1) {
      const ax = routeLocal[i - 1][0];
      const ay = routeLocal[i - 1][1];
      const bx = routeLocal[i][0];
      const by = routeLocal[i][1];
      const vx = bx - ax;
      const vy = by - ay;
      const len2 = vx * vx + vy * vy;
      let t = 0;
      if (len2 > 1e-6) {
        t = ((s.x - ax) * vx + (s.y - ay) * vy) / len2;
        t = Math.max(0, Math.min(1, t));
      }
      const px = ax + t * vx;
      const py = ay + t * vy;
      if ((px - s.x) * (px - s.x) + (py - s.y) * (py - s.y) < 0.5) {
        return cumDistM[i - 1] + t * (cumDistM[i] - cumDistM[i - 1]);
      }
    }
    return 0;
  }

  function trafficPoses() {
    const poses = [];
    if (!ready) {
      return poses;
    }
    trafficVehicles.forEach((v) => {
      const pos = interpolateAt(v.dist);
      if (!pos) {
        return;
      }
      const nx = -Math.sin(pos.yawRad);
      const ny = Math.cos(pos.yawRad);
      const ll = api.localToLngLat(pos.x + nx * v.lane, pos.y + ny * v.lane);
      poses.push({ lng: ll[0], lat: ll[1], bearing: pos.bearing });
    });
    return poses;
  }

  function trafficFeatureCollection() {
    const features = [];
    if (!ready) {
      return { type: 'FeatureCollection', features };
    }
    trafficVehicles.forEach((v) => {
      const pos = interpolateAt(v.dist);
      if (!pos) {
        return;
      }
      const nx = -Math.sin(pos.yawRad);
      const ny = Math.cos(pos.yawRad);
      const ll = api.localToLngLat(pos.x + nx * v.lane, pos.y + ny * v.lane);
      features.push({
        type: 'Feature',
        properties: { class: 'vehicle', label: 'vehicle', radius: 2.7, bearing: pos.bearing },
        geometry: { type: 'Point', coordinates: ll },
      });
    });
    return { type: 'FeatureCollection', features };
  }

  function syntheticPerceptionFeature() {
    return trafficFeatureCollection();
  }

  function angleDiffDeg(fromDeg, toDeg) {
    return ((toDeg - fromDeg + 540) % 360) - 180;
  }

  /** Turn arrows + distance for 1st-person HUD (samples bearing ahead on route). */
  function navGuidanceAt(distAlongM) {
    if (!ready) {
      return { arrows: '↑  ↑  ↑', distanceText: '—' };
    }
    const now = interpolateAt(distAlongM);
    if (!now) {
      return { arrows: '↑  ↑  ↑', distanceText: '—' };
    }
    const scanStep = 10;
    const scanMax = Math.min(320, Math.max(0, totalLenM - distAlongM));
    let turnDist = scanMax;
    let turnDir = 0;
    let prevB = now.bearing;
    for (let d = scanStep; d <= scanMax; d += scanStep) {
      const p = interpolateAt(distAlongM + d);
      if (!p) {
        break;
      }
      const delta = angleDiffDeg(prevB, p.bearing);
      if (Math.abs(delta) > 16) {
        turnDist = d;
        turnDir = delta > 0 ? 1 : -1;
        break;
      }
      prevB = p.bearing;
    }
    const arrowChar = turnDir < 0 ? '↰' : turnDir > 0 ? '↱' : '↑';
    const arrows = turnDir === 0 ? '↑  ↑  ↑' : `${arrowChar}  ↑  ↑`;
    let distanceText;
    if (turnDir === 0) {
      const remain = Math.max(0, totalLenM - distAlongM);
      distanceText = remain >= 1000 ? `${(remain / 1000).toFixed(1)} km` : `${Math.round(remain)} m`;
    } else {
      distanceText = turnDist >= 1000 ? `${(turnDist / 1000).toFixed(1)} km` : `${Math.round(turnDist)} m`;
    }
    return { arrows, distanceText };
  }

  return {
    loadRoute,
    ensureOfflineRoute,
    loadFallbackRoute,
    isReady,
    onReady,
    snapLocal,
    interpolateAt,
    nearestDistanceAlongRoute,
    routeLineFeatureLngLat,
    aheadTrajectoryFeature,
    laneBandFeature,
    roadCorridorFeature,
    roadLaneSpec,
    roadHalfWidthM,
    laneMarkingsFeatureCollection,
    markExternalEgo,
    externalEgoActive,
    startDemo,
    stopDemo,
    setDriveDistance,
    getDriveDistance,
    isAtRouteEnd,
    clampAlong,
    tickTraffic,
    trafficPoses,
    trafficFeatureCollection,
    syntheticPerceptionFeature,
    navGuidanceAt,
    getRouteSource,
    get totalLenM() {
      return totalLenM;
    },
  };
})(window.SdvMapbox);
