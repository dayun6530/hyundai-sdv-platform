/* global mapboxgl, SdvMapbox, SdvRouteSnap, SdvVehicleLayer, THREE */
window.SdvMapbox = (function (api) {
  let map = null;
  let mapboxBridge = null;
  let cameraMode = 'first_person';
  const pending = { ego: null, trajectory: null, perception: null, plannedRoute: null };
  let lastEgo = null;
  let lastCamJumpMs = 0;
  let routeSetupDone = false;
  let sceneryReady = false;
  let drivingEnvReady = false;
  let vehicle3dOk = false;
  let smoothBearingDeg = null;
  const DRIVE_DT_S = 0.2;
  let manualControlsReady = false;
  let manualAnimId = null;
  let manualOverride = false;
  let manualX = 0;
  let manualY = 0;
  let manualYawRad = 0;
  const keysDown = { ArrowUp: false, ArrowDown: false, ArrowLeft: false, ArrowRight: false };
  const MANUAL_SPEED_MPS = 18;
  const MANUAL_REVERSE_MPS = 10;
  const MANUAL_TURN_RAD_S = 1.65;

  /** Wide route ribbon under the vehicle (meters). */
  const SIM_CAM = {
    corridorBehindM: 22,
    corridorAheadM: 240,
  };

  /** Mapbox-style 3D Live Navigation chase cam (reference: behind car, road ahead). */
  const LIVE_NAV = {
    backFirstM: 14.5,
    backThirdM: 21,
    lookAheadM: 34,
    pitch: 61,
    zoom: 18.02,
    padding: { top: 52, bottom: 230, left: 0, right: 0 },
  };

  function isLiveNavMode() {
    return cameraMode === 'first_person' || cameraMode === 'third_person';
  }

  function showBanner(text, isError) {
    const banner = document.getElementById('banner');
    if (!banner) return;
    banner.textContent = text;
    banner.style.display = 'block';
    banner.style.color = isError ? '#fecaca' : '#fbbf24';
    banner.style.borderColor = isError ? '#ef4444' : '#f59e0b';
  }

  function hideBanner() {
    const banner = document.getElementById('banner');
    if (banner) banner.style.display = 'none';
  }

  function normalizeCameraMode(mode) {
    const m = String(mode || '').toLowerCase();
    if (m.includes('1st') || m === 'first_person') return 'first_person';
    if (m.includes('3rd') || m === 'third_person') return 'third_person';
    if (m.includes('dashboard')) return 'dashboard';
    if (m.includes('operations')) return 'operations';
    return 'first_person';
  }

  function resolvePose(x, y, yawRad, allowBackward) {
    if (SdvRouteSnap.isReady()) {
      const s = SdvRouteSnap.snapLocal(x, y, allowBackward);
      if (s.snapped) {
        return { x: s.x, y: s.y, yawRad: s.yawRad, bearing: s.bearing, lng: s.lng, lat: s.lat };
      }
    }
    const ll = api.localToLngLat(x, y);
    return { x, y, yawRad, bearing: api.yawToBearingDeg(yawRad), lng: ll[0], lat: ll[1] };
  }

  function setEgoLayerVisibility(visible) {
    if (!map) return;
    ['ego-glow', 'ego-vehicle-2d', 'ego-body', 'ego-arrow'].forEach((id) => {
      if (map.getLayer(id)) {
        map.setLayoutProperty(id, 'visibility', visible ? 'visible' : 'none');
      }
    });
  }

  function add3dBuildings() {
    if (!map || map.getLayer('sdv-3d-buildings') || !map.getSource('composite')) {
      return;
    }
    map.addLayer({
      id: 'sdv-3d-buildings',
      source: 'composite',
      'source-layer': 'building',
      filter: ['==', 'extrude', 'true'],
      type: 'fill-extrusion',
      minzoom: 14,
      paint: {
        'fill-extrusion-color': '#c8c2b6',
        'fill-extrusion-height': ['*', ['coalesce', ['get', 'height'], 14], 1.1],
        'fill-extrusion-base': ['coalesce', ['get', 'min_height'], 0],
        'fill-extrusion-opacity': 0.88,
        'fill-extrusion-vertical-gradient': true,
      },
    });
  }

  function addSkyFog() {
    if (!map || typeof map.setFog !== 'function') {
      return;
    }
    map.setFog({
      color: 'rgb(186, 198, 210)',
      'high-color': 'rgb(218, 226, 236)',
      'horizon-blend': 0.18,
      'space-color': 'rgb(160, 175, 195)',
      'star-intensity': 0,
    });
  }

  function addDrivingRoadLayers() {
    if (!map || !map.getSource('composite')) {
      return;
    }
    const beforeId = map.getLayer('sdv-3d-buildings') ? 'sdv-3d-buildings' : undefined;
    if (!map.getLayer('sdv-road-surface')) {
      map.addLayer(
        {
          id: 'sdv-road-surface',
          source: 'composite',
          'source-layer': 'road',
          type: 'line',
          minzoom: 15.5,
          filter: [
            'match',
            ['get', 'class'],
            ['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'street', 'street_limited'],
            true,
            false,
          ],
          paint: {
            'line-color': '#1e293b',
            'line-width': ['interpolate', ['linear'], ['zoom'], 16, 16, 18, 24, 20, 32],
            'line-opacity': 0.18,
          },
        },
        beforeId
      );
    }
  }

  function addDrivingEnvironment() {
    if (!map || drivingEnvReady) {
      return;
    }
    if (typeof map.setTerrain === 'function') {
      map.setTerrain(null);
    }
    addSkyFog();
    addDrivingRoadLayers();
    drivingEnvReady = true;
  }

  function setSimBuildingVisibility(mode) {
    if (!map || !map.getLayer('sdv-3d-buildings')) {
      return;
    }
    const live = mode === 'first_person' || mode === 'third_person';
    if (live) {
      map.setLayoutProperty('sdv-3d-buildings', 'visibility', 'visible');
      return;
    }
    map.setLayoutProperty('sdv-3d-buildings', 'visibility', 'visible');
  }

  function lerpBearingDeg(from, to, t) {
    const delta = ((to - from + 540) % 360) - 180;
    return (from + delta * t + 360) % 360;
  }

  function refreshTraffic3d() {
    if (!SdvRouteSnap.isReady()) {
      return;
    }
    SdvRouteSnap.tickTraffic(DRIVE_DT_S);
    if (typeof SdvVehicleLayer !== 'undefined' && vehicle3dOk) {
      SdvVehicleLayer.setTrafficVisible(cameraMode === 'first_person' || cameraMode === 'third_person');
      if (typeof SdvRouteSnap.trafficPoses === 'function') {
        SdvVehicleLayer.updateTraffic(SdvRouteSnap.trafficPoses());
      }
    }
  }

  function addSceneryLayers() {
    if (!map || sceneryReady) return;
    if (map.getSource('composite') && !map.getLayer('sdv-parks')) {
      map.addLayer({
        id: 'sdv-parks',
        source: 'composite',
        'source-layer': 'landuse',
        filter: ['match', ['get', 'class'], ['park', 'grass', 'wood', 'scrub'], true, false],
        type: 'fill',
        paint: { 'fill-color': '#15803d', 'fill-opacity': 0.5 },
      });
    }
    add3dBuildings();
    addDrivingEnvironment();
    sceneryReady = true;
  }

  function ensureRouteLayers() {
    if (!map.getSource('driving-route')) {
      map.addSource('driving-route', {
        type: 'geojson',
        data: { type: 'Feature', geometry: { type: 'LineString', coordinates: [] } },
      });
    }
    if (!map.getSource('driving-corridor')) {
      map.addSource('driving-corridor', {
        type: 'geojson',
        data: { type: 'Feature', geometry: { type: 'Polygon', coordinates: [[]] } },
      });
    }
    if (!map.getSource('driving-lane-marks')) {
      map.addSource('driving-lane-marks', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
    }
    if (!map.getLayer('driving-corridor-fill')) {
      map.addLayer({
        id: 'driving-corridor-fill',
        type: 'fill',
        source: 'driving-corridor',
        paint: {
          'fill-color': '#6d7585',
          'fill-opacity': 0.92,
        },
      });
    }
    if (!map.getLayer('driving-lane-edge')) {
      map.addLayer({
        id: 'driving-lane-edge',
        type: 'line',
        source: 'driving-lane-marks',
        filter: ['==', ['get', 'kind'], 'edge'],
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: {
          'line-color': '#f8fafc',
          'line-width': ['interpolate', ['linear'], ['zoom'], 17, 2.4, 19, 3.6, 20, 4.2],
          'line-opacity': 0.95,
        },
      });
    }
    if (!map.getLayer('driving-lane-dash')) {
      map.addLayer({
        id: 'driving-lane-dash',
        type: 'line',
        source: 'driving-lane-marks',
        filter: ['==', ['get', 'kind'], 'dash'],
        layout: { 'line-cap': 'butt', 'line-join': 'round' },
        paint: {
          'line-color': '#ffffff',
          'line-width': ['interpolate', ['linear'], ['zoom'], 17, 1.6, 19, 2.4, 20, 2.8],
          'line-opacity': 0.92,
          'line-dasharray': [1.2, 1.8],
        },
      });
    }
    if (!map.getLayer('driving-route-line')) {
      map.addLayer({
        id: 'driving-route-line',
        type: 'line',
        source: 'driving-route',
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: {
          'line-color': '#e2e8f0',
          'line-width': ['interpolate', ['linear'], ['zoom'], 17, 1.2, 19, 1.8],
          'line-opacity': 0.35,
        },
      });
    }
  }

  function publishNavGuidance(distAlongM) {
    if (!mapboxBridge || typeof mapboxBridge.publishNavGuidance !== 'function') {
      return;
    }
    if (!SdvRouteSnap.isReady() || typeof SdvRouteSnap.navGuidanceAt !== 'function') {
      return;
    }
    const g = SdvRouteSnap.navGuidanceAt(distAlongM);
    mapboxBridge.publishNavGuidance(g.arrows, g.distanceText);
  }

  function ensureLayers() {
    ensureRouteLayers();
    // Synthetic traffic markers disabled (were gray/white circles on the map).
    if (!map.getSource('perception')) {
      map.addSource('perception', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
    }
    if (!map.getLayer('perception-circle')) {
      map.addLayer({
        id: 'perception-circle',
        type: 'circle',
        source: 'perception',
        paint: {
          'circle-radius': [
            'case',
            ['==', ['get', 'class'], 'traffic_light'],
            9,
            ['coalesce', ['get', 'radius'], 5],
          ],
          'circle-color': [
            'match',
            ['get', 'tl_state'],
            'red',
            '#ef4444',
            'yellow',
            '#facc15',
            'green',
            '#4ade80',
            '#f97316',
          ],
          'circle-opacity': 0.82,
          'circle-stroke-width': 1,
          'circle-stroke-color': '#f8fafc',
        },
      });
    }
    if (!map.getSource('trajectory')) {
      map.addSource('trajectory', {
        type: 'geojson',
        data: { type: 'Feature', geometry: { type: 'LineString', coordinates: [] } },
      });
      map.addLayer({
        id: 'trajectory-line',
        type: 'line',
        source: 'trajectory',
        paint: { 'line-color': '#4ade80', 'line-width': 3, 'line-opacity': 0.85 },
      });
    }
    if (!map.getSource('planned-route')) {
      map.addSource('planned-route', {
        type: 'geojson',
        data: { type: 'Feature', geometry: { type: 'LineString', coordinates: [] } },
      });
      map.addLayer({
        id: 'planned-route-line',
        type: 'line',
        source: 'planned-route',
        paint: { 'line-color': '#22d3ee', 'line-width': 4, 'line-opacity': 0.9 },
      });
    }
    if (!map.getSource('ego')) {
      map.addSource('ego', {
        type: 'geojson',
        data: {
          type: 'Feature',
          geometry: { type: 'Point', coordinates: api.mapOrigin() },
          properties: { bearing: 0 },
        },
      });
      map.addLayer({
        id: 'ego-glow',
        type: 'circle',
        source: 'ego',
        paint: { 'circle-radius': 14, 'circle-color': '#3b82f6', 'circle-opacity': 0.25 },
      });
      map.addLayer({
        id: 'ego-vehicle-2d',
        type: 'circle',
        source: 'ego',
        layout: { visibility: 'none' },
        paint: {
          'circle-radius': 11,
          'circle-color': '#f8fafc',
          'circle-stroke-width': 2,
          'circle-stroke-color': '#cbd5e1',
        },
      });
    }
  }

  function installVehicle() {
    vehicle3dOk = false;
    if (map.getLayer('ego-vehicle-2d')) {
      map.setLayoutProperty('ego-vehicle-2d', 'visibility', 'visible');
    }
    if (typeof THREE !== 'undefined' && typeof SdvVehicleLayer !== 'undefined') {
      try {
        SdvVehicleLayer.install(map);
        vehicle3dOk = true;
        if (map.getLayer('ego-vehicle-2d')) {
          map.setLayoutProperty('ego-vehicle-2d', 'visibility', 'none');
        }
      } catch (err) {
        console.warn('3D vehicle install failed', err);
      }
    }
  }

  function updateLaneCountLabel(lanesOneWay) {
    const el = document.getElementById('lane-count-label');
    if (!el) {
      return;
    }
    const n = Math.max(1, lanesOneWay || 3);
    el.textContent = `${n}차선 (편도)`;
  }

  function refreshRouteLayers(distAlongM) {
    if (!map || !SdvRouteSnap.isReady()) return;
    map.getSource('driving-route').setData(SdvRouteSnap.routeLineFeatureLngLat());
    const along = typeof distAlongM === 'number' ? distAlongM : SdvRouteSnap.getDriveDistance();
    const spec = typeof SdvRouteSnap.roadLaneSpec === 'function' ? SdvRouteSnap.roadLaneSpec() : { lanesOneWay: 3, laneWidthM: 3.5 };
    const halfW =
      typeof SdvRouteSnap.roadHalfWidthM === 'function'
        ? SdvRouteSnap.roadHalfWidthM(spec.lanesOneWay, spec.laneWidthM)
        : 5.25;
    const from = Math.max(0, along - SIM_CAM.corridorBehindM);
    if (map.getSource('driving-corridor') && typeof SdvRouteSnap.roadCorridorFeature === 'function') {
      const corridor = SdvRouteSnap.roadCorridorFeature(from, SIM_CAM.corridorAheadM, halfW, 56);
      if (corridor) {
        map.getSource('driving-corridor').setData(corridor);
      }
    }
    if (map.getSource('driving-lane-marks') && typeof SdvRouteSnap.laneMarkingsFeatureCollection === 'function') {
      map.getSource('driving-lane-marks').setData(
        SdvRouteSnap.laneMarkingsFeatureCollection(from, SIM_CAM.corridorAheadM, spec.lanesOneWay, spec.laneWidthM)
      );
    }
    updateLaneCountLabel(spec.lanesOneWay);
  }

  function initManualPose() {
    if (SdvRouteSnap.isReady()) {
      const pos = SdvRouteSnap.interpolateAt(SdvRouteSnap.getDriveDistance());
      if (pos) {
        manualX = pos.x;
        manualY = pos.y;
        manualYawRad = pos.yawRad;
        return;
      }
    }
    manualX = 0;
    manualY = 0;
    manualYawRad = 0;
  }

  function manualPoseFromState() {
    const ll = api.localToLngLat(manualX, manualY);
    return {
      x: manualX,
      y: manualY,
      yawRad: manualYawRad,
      bearing: api.yawToBearingDeg(manualYawRad),
      lng: ll[0],
      lat: ll[1],
    };
  }

  function manualTick(dt) {
    if (!map || !map.isStyleLoaded()) {
      return;
    }
    if (!map.getSource('ego')) {
      ensureLayers();
    }
    let fwd = 0;
    if (keysDown.ArrowUp) {
      fwd += 1;
    }
    if (keysDown.ArrowDown) {
      fwd -= 1;
    }
    if (keysDown.ArrowLeft) {
      manualYawRad += MANUAL_TURN_RAD_S * dt;
    }
    if (keysDown.ArrowRight) {
      manualYawRad -= MANUAL_TURN_RAD_S * dt;
    }
    if (fwd !== 0) {
      const speed = fwd > 0 ? MANUAL_SPEED_MPS : MANUAL_REVERSE_MPS;
      manualX += Math.cos(manualYawRad) * speed * fwd * dt;
      manualY += Math.sin(manualYawRad) * speed * fwd * dt;
    }
    const pose = manualPoseFromState();
    followRoutePose(pose, false);
    if (SdvRouteSnap.isReady()) {
      SdvRouteSnap.tickTraffic(dt);
      if (map.getSource('traffic')) {
        map.getSource('traffic').setData(SdvRouteSnap.trafficFeatureCollection());
      }
    }
  }

  function startManualDriveLoop() {
    if (manualAnimId !== null) {
      return;
    }
    let lastT = performance.now();
    function frame(now) {
      const dt = Math.min(0.05, (now - lastT) / 1000);
      lastT = now;
      manualTick(dt);
      manualAnimId = requestAnimationFrame(frame);
    }
    manualAnimId = requestAnimationFrame(frame);
  }

  function setupManualControls() {
    if (manualControlsReady) {
      return;
    }
    manualControlsReady = true;
    const focusTarget = document.body;
    focusTarget.setAttribute('tabindex', '0');
    focusTarget.style.outline = 'none';

    function onKeyDown(e) {
      if (!Object.prototype.hasOwnProperty.call(keysDown, e.key)) {
        return;
      }
      e.preventDefault();
      keysDown[e.key] = true;
    }
    function onKeyUp(e) {
      if (!Object.prototype.hasOwnProperty.call(keysDown, e.key)) {
        return;
      }
      keysDown[e.key] = false;
    }
    window.addEventListener('keydown', onKeyDown, true);
    window.addEventListener('keyup', onKeyUp, true);
    map.getCanvas().addEventListener('click', () => {
      focusTarget.focus();
    });
    setTimeout(() => focusTarget.focus(), 600);
  }

  function finishRouteSetup() {
    if (!map || !map.isStyleLoaded() || !SdvRouteSnap.isReady()) {
      return false;
    }
    if (!routeSetupDone) {
      routeSetupDone = true;
      ensureLayers();
      addSceneryLayers();
      refreshRouteLayers();
      installVehicle();
      refreshTraffic3d();
      publishNavGuidance(SdvRouteSnap.getDriveDistance());
      updateWindshield(cameraMode);
      const tag = document.getElementById('map-build-tag');
      if (tag) {
        tag.style.display = 'none';
      }
      hideBanner();
    }
    return true;
  }

  function autoDriveTick(pos, routeReset, distAlongM) {
    if (!map || !map.isStyleLoaded()) {
      return;
    }
    ensureLayers();
    if (routeReset) {
      lastCamJumpMs = Date.now();
    }
    const along = typeof distAlongM === 'number' ? distAlongM : SdvRouteSnap.getDriveDistance();
    refreshRouteLayers(along);
    refreshTraffic3d();
    followRoutePose(pos, routeReset);
    publishNavGuidance(along);
  }

  function alignMapToRouteStart() {
    const pos = SdvRouteSnap.interpolateAt(0);
    if (!map || !pos) {
      return;
    }
    applySimulatorCamera(pos, true);
  }

  function onRouteReady(token) {
    SdvRouteSnap.ensureOfflineRoute();
    if (map && map.isStyleLoaded()) {
      refreshRouteLayers();
      alignMapToRouteStart();
      finishRouteSetup();
      hideBanner();
    }
    SdvRouteSnap.loadRoute(token).then(() => {
      if (!map || !map.isStyleLoaded()) {
        return;
      }
      refreshRouteLayers();
      smoothBearingDeg = null;
      alignMapToRouteStart();
      setSimBuildingVisibility(cameraMode);
      hideBanner();
    });
  }

  window.__sdvKickCockpit = function sdvKickCockpit() {
    if (!map) {
      return 'map not ready';
    }
    SdvRouteSnap.ensureOfflineRoute();
    if (!SdvRouteSnap.isReady()) {
      return 'route loading';
    }
    alignMapToRouteStart();
    const ok = finishRouteSetup();
    return ok ? 'auto drive ready' : 'setup pending';
  };

  function applyTrajectoryGeoJson(geoJson) {
    if (!map || !geoJson) {
      return;
    }
    ensureLayers();
    try {
      let feat = JSON.parse(geoJson);
      if (feat.geometry && feat.geometry.coordinates && feat.geometry.coordinates.length) {
        const c0 = feat.geometry.coordinates[0];
        if (Array.isArray(c0) && Math.abs(c0[0]) < 5000 && Math.abs(c0[1]) < 5000) {
          feat = api.transformLineFeature(feat);
        }
      }
      map.getSource('trajectory').setData(feat);
    } catch (err) {
      console.warn('trajectory parse', err);
    }
  }

  function applyPerceptionGeoJson(geoJson) {
    if (!map || !geoJson) {
      return;
    }
    ensureLayers();
    try {
      let fc = JSON.parse(geoJson);
      if (fc.features && fc.features.length) {
        const c = fc.features[0].geometry && fc.features[0].geometry.coordinates;
        if (c && Math.abs(c[0]) < 5000) {
          fc = api.transformFeatureCollection(fc);
        }
      }
      map.getSource('perception').setData(fc);
    } catch (err) {
      console.warn('perception parse', err);
    }
  }

  function applyPlannedRouteGeoJson(geoJson) {
    if (!map || !geoJson) {
      return;
    }
    ensureLayers();
    try {
      const feat = JSON.parse(geoJson);
      map.getSource('planned-route').setData(feat);
    } catch (err) {
      console.warn('planned route parse', err);
    }
  }

  function flushPending() {
    if (pending.ego) {
      followEgo(pending.ego.x, pending.ego.y, pending.ego.yawRad, true, false);
      pending.ego = null;
    }
    if (pending.trajectory) {
      applyTrajectoryGeoJson(pending.trajectory);
      pending.trajectory = null;
    }
    if (pending.perception) {
      applyPerceptionGeoJson(pending.perception);
      pending.perception = null;
    }
    if (pending.plannedRoute) {
      applyPlannedRouteGeoJson(pending.plannedRoute);
      pending.plannedRoute = null;
    }
  }

  function updateWindshield(mode) {
    const el = document.getElementById('windshield');
    const sill = document.getElementById('dash-sill');
    const label = document.getElementById('live-nav-label');
    const laneTag = document.getElementById('lane-count-label');
    const live = isLiveNavMode();
    document.body.classList.toggle('live-nav', live);
    document.body.classList.remove('sim-drive');
    if (el) {
      el.style.opacity = live ? '0' : '0.35';
      el.style.display = live ? 'none' : 'block';
    }
    if (sill) {
      sill.style.display = live ? 'none' : 'block';
    }
    if (label) {
      label.style.display = live ? 'block' : 'none';
    }
    if (laneTag) {
      laneTag.style.display = live ? 'block' : 'none';
    }
    setSimBuildingVisibility(mode);
  }

  /** 3D Live Navigation — chase cam behind the car (reference layout). */
  function chaseCameraLiveNav(pose, backM) {
    const cos = Math.cos(pose.yawRad);
    const sin = Math.sin(pose.yawRad);
    const center = api.localToLngLat(
      pose.x + LIVE_NAV.lookAheadM * cos - backM * cos,
      pose.y + LIVE_NAV.lookAheadM * sin - backM * sin
    );
    map.jumpTo({
      center,
      bearing: pose.bearing,
      pitch: LIVE_NAV.pitch,
      zoom: LIVE_NAV.zoom,
      padding: LIVE_NAV.padding,
    });
  }

  function applySimulatorCamera(pose, instant) {
    if (!map || !pose) {
      return;
    }
    if (!isLiveNavMode()) {
      map.easeTo({
        center: [pose.lng, pose.lat],
        bearing: cameraMode === 'operations' ? pose.bearing : 0,
        pitch: cameraMode === 'operations' ? 25 : 0,
        zoom: cameraMode === 'operations' ? 16.2 : 15.2,
        duration: 280,
        essential: true,
      });
      return;
    }
    if (instant || smoothBearingDeg === null) {
      smoothBearingDeg = pose.bearing;
    } else {
      smoothBearingDeg = lerpBearingDeg(smoothBearingDeg, pose.bearing, 0.16);
    }
    const yawRad = api.bearingToYawRad(smoothBearingDeg);
    const backM = cameraMode === 'third_person' ? LIVE_NAV.backThirdM : LIVE_NAV.backFirstM;
    chaseCameraLiveNav(
      { x: pose.x, y: pose.y, yawRad, bearing: smoothBearingDeg },
      backM
    );
  }

  /** Ego on route polyline — no re-snap (stays on road centerline). */
  function followRoutePose(pos, routeReset) {
    const pose = {
      x: pos.x,
      y: pos.y,
      yawRad: pos.yawRad,
      bearing: pos.bearing,
      lng: pos.lng,
      lat: pos.lat,
    };
    lastEgo = { x: pose.x, y: pose.y, yawRad: pose.yawRad };

    const showVehicleMesh = vehicle3dOk && isLiveNavMode();
    if (typeof SdvVehicleLayer !== 'undefined') {
      SdvVehicleLayer.setVisible(showVehicleMesh);
      SdvVehicleLayer.updatePose(pose.lng, pose.lat, pose.bearing);
      SdvVehicleLayer.setTrafficVisible(isLiveNavMode());
    }
    if (!vehicle3dOk && map.getLayer('ego-vehicle-2d')) {
      map.setLayoutProperty('ego-vehicle-2d', 'visibility', isLiveNavMode() ? 'visible' : 'none');
    }

    map.getSource('ego').setData({
      type: 'Feature',
      geometry: { type: 'Point', coordinates: [pose.lng, pose.lat] },
      properties: { bearing: pose.bearing },
    });

    updateWindshield(cameraMode);
    setEgoLayerVisibility(false);

    if (cameraMode === 'first_person' || cameraMode === 'third_person') {
      applySimulatorCamera(pose, routeReset || Date.now() - lastCamJumpMs < 350);
    }
  }

  function followEgo(x, y, yawRad, fromExternal, routeReset) {
    if (fromExternal) {
      SdvRouteSnap.markExternalEgo();
      SdvRouteSnap.stopDemo();
    }
    const pose = resolvePose(x, y, yawRad, fromExternal);
    lastEgo = { x: pose.x, y: pose.y, yawRad: pose.yawRad };
    const egoLngLat = [pose.lng, pose.lat];

    if (typeof SdvVehicleLayer !== 'undefined') {
      SdvVehicleLayer.setVisible(
        vehicle3dOk && cameraMode !== 'dashboard' && cameraMode !== 'operations'
      );
      SdvVehicleLayer.updatePose(pose.lng, pose.lat, pose.bearing);
    }
    if (!vehicle3dOk && map.getLayer('ego-vehicle-2d')) {
      map.setLayoutProperty(
        'ego-vehicle-2d',
        'visibility',
        cameraMode === 'first_person' || cameraMode === 'third_person' ? 'visible' : 'none'
      );
    }

    map.getSource('ego').setData({
      type: 'Feature',
      geometry: { type: 'Point', coordinates: egoLngLat },
      properties: { bearing: pose.bearing },
    });

    updateWindshield(cameraMode);
    setEgoLayerVisibility(false);

    if (cameraMode === 'first_person' || cameraMode === 'third_person') {
      applySimulatorCamera(pose, routeReset || Date.now() - lastCamJumpMs < 350);
      return;
    }

    setEgoLayerVisibility(true);
    map.easeTo({
      center: egoLngLat,
      bearing: cameraMode === 'operations' ? pose.bearing : 0,
      pitch: cameraMode === 'operations' ? 25 : 0,
      zoom: cameraMode === 'operations' ? 16.2 : 15.2,
      duration: 280,
      essential: true,
    });
  }

  function onMapStyleReady() {
    if (!map || !map.isStyleLoaded()) return;
    sceneryReady = false;
    drivingEnvReady = false;
    ensureLayers();
    setEgoLayerVisibility(false);
    SdvRouteSnap.ensureOfflineRoute();
    finishRouteSetup();
    hideBanner();
    SdvRouteSnap.onReady(() => {
      if (!map || !map.isStyleLoaded()) {
        return;
      }
      refreshRouteLayers();
      alignMapToRouteStart();
    });
    flushPending();
  }

  window.__sdvDriveFrame = function sdvDriveFrame(distM) {
    if (!map || !map.isStyleLoaded()) {
      return 'style loading';
    }
    if (!SdvRouteSnap.isReady()) {
      return 'route loading';
    }
    if (!routeSetupDone) {
      finishRouteSetup();
    }
    SdvRouteSnap.setDriveDistance(distM);
    const along = SdvRouteSnap.getDriveDistance();
    const pos = SdvRouteSnap.interpolateAt(along);
    if (!pos) {
      return 'no pos';
    }
    autoDriveTick(pos, false, along);
    return SdvRouteSnap.isAtRouteEnd() ? 'end' : 'ok';
  };

  function initMap(token) {
    if (map) return;
    map = new mapboxgl.Map({
      container: 'map',
      style: 'mapbox://styles/mapbox/navigation-day-v1',
      center: api.mapOrigin(),
      zoom: LIVE_NAV.zoom,
      pitch: LIVE_NAV.pitch,
      bearing: 0,
      antialias: true,
      attributionControl: false,
    });

    map.once('load', () => {
      onMapStyleReady();
      onRouteReady(token);
    });

    map.on('error', (e) => {
      const msg = e && e.error && e.error.message ? e.error.message : '';
      if (msg.includes('meshes is not iterable')) return;
      if (msg) showBanner('Mapbox: ' + msg, true);
    });
  }

  function bootMain() {
    api.initBridge().then((bridge) => {
      mapboxBridge = bridge;
      cameraMode = normalizeCameraMode(mapboxBridge.cameraMode || 'first_person');
      updateWindshield(cameraMode);

      mapboxBridge.cameraModeChanged.connect((mode) => {
        cameraMode = normalizeCameraMode(mode);
        smoothBearingDeg = null;
        updateWindshield(cameraMode);
        if (lastEgo) followEgo(lastEgo.x, lastEgo.y, lastEgo.yawRad, false, false);
      });

      mapboxBridge.mapboxTokenReady.connect((token) => {
        if (!token || !String(token).startsWith('pk.')) {
          const warn =
            mapboxBridge.mapboxTokenWarning && mapboxBridge.mapboxTokenWarning()
              ? mapboxBridge.mapboxTokenWarning()
              : '';
          showBanner(
            'Mapbox 토큰 없음 — sdv-dashboard-qt\\.mapbox_token 파일에 pk. 토큰을 저장하세요.' +
              (warn ? ' (' + warn + ')' : ''),
            true
          );
          return;
        }
        mapboxgl.accessToken = token;
        initMap(token);
      });

      mapboxBridge.egoPoseChanged.connect((x, y, z, yawRad) => {
        if (!map || !map.isStyleLoaded()) {
          pending.ego = { x, y, yawRad };
          return;
        }
        followEgo(x, y, yawRad, true, false);
      });

      mapboxBridge.trajectoryChanged.connect((geoJson) => {
        if (!map || !map.isStyleLoaded()) {
          pending.trajectory = geoJson;
          return;
        }
        applyTrajectoryGeoJson(geoJson);
      });

      mapboxBridge.perceptionChanged.connect((geoJson) => {
        if (!map || !map.isStyleLoaded()) {
          pending.perception = geoJson;
          return;
        }
        applyPerceptionGeoJson(geoJson);
      });

      mapboxBridge.plannedRouteChanged.connect((geoJson) => {
        if (!map || !map.isStyleLoaded()) {
          pending.plannedRoute = geoJson;
          return;
        }
        applyPlannedRouteGeoJson(geoJson);
      });

      mapboxBridge.notifyTokenReady();
    });
  }

  api.bootMain = bootMain;
  return api;
})(window.SdvMapbox || {});
