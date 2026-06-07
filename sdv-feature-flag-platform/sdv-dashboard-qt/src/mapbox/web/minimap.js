/* global mapboxgl, SdvMapbox, SdvRouteSnap */
window.SdvMapbox = (function (api) {
  let map = null;
  let routeToken = '';

  function ensureLayers() {
    if (!map.getSource('driving-route')) {
      map.addSource('driving-route', {
        type: 'geojson',
        data: { type: 'Feature', geometry: { type: 'LineString', coordinates: [] } },
      });
      map.addLayer({
        id: 'driving-route-line',
        type: 'line',
        source: 'driving-route',
        paint: { 'line-color': '#38bdf8', 'line-width': 2, 'line-opacity': 0.4 },
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

    if (!map.getSource('perception')) {
      map.addSource('perception', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: 'perception-circle',
        type: 'circle',
        source: 'perception',
        paint: {
          'circle-radius': [
            'case',
            ['==', ['get', 'class'], 'traffic_light'],
            8,
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
          'circle-opacity': 0.75,
          'circle-stroke-width': 1,
          'circle-stroke-color': '#fdba74',
        },
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

    if (!map.getSource('destination')) {
      map.addSource('destination', {
        type: 'geojson',
        data: { type: 'Feature', geometry: { type: 'Point', coordinates: api.mapOrigin() } },
      });
      map.addLayer({
        id: 'destination-pin',
        type: 'circle',
        source: 'destination',
        paint: {
          'circle-radius': 8,
          'circle-color': '#22d3ee',
          'circle-stroke-width': 2,
          'circle-stroke-color': '#e0f2fe',
        },
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
        id: 'ego-dot',
        type: 'circle',
        source: 'ego',
        paint: {
          'circle-radius': 7,
          'circle-color': '#38bdf8',
          'circle-stroke-width': 2,
          'circle-stroke-color': '#e0f2fe',
        },
      });
      map.addLayer({
        id: 'ego-heading',
        type: 'symbol',
        source: 'ego',
        layout: {
          'text-field': '▲',
          'text-size': 14,
          'text-rotate': ['coalesce', ['get', 'bearing'], 0],
          'text-rotation-alignment': 'map',
          'text-allow-overlap': true,
          'text-offset': [0, -0.5],
        },
        paint: { 'text-color': '#7dd3fc' },
      });
    }
  }

  function resolvePose(x, y, yawRad) {
    if (SdvRouteSnap.isReady()) {
      const s = SdvRouteSnap.snapLocal(x, y);
      if (s.snapped) {
        return s;
      }
    }
    return { x, y, yawRad, bearing: api.yawToBearingDeg(yawRad) };
  }

  function bootMini(containerId) {
    api.initBridge().then((mapboxBridge) => {
      mapboxBridge.mapboxTokenReady.connect((token) => {
        if (!token) {
          return;
        }
        routeToken = token;
        mapboxgl.accessToken = token;
        if (map) {
          return;
        }
        map = new mapboxgl.Map({
          container: containerId,
          style: 'mapbox://styles/mapbox/dark-v11',
          center: api.mapOrigin(),
          zoom: 15,
          pitch: 0,
          bearing: 0,
          interactive: true,
          attributionControl: false,
        });
        map.on('load', () => {
          ensureLayers();
          map.on('click', (e) => {
            if (!e.originalEvent.shiftKey) {
              return;
            }
            const local = api.lngLatToLocal(e.lngLat.lng, e.lngLat.lat);
            map.getSource('destination').setData({
              type: 'Feature',
              geometry: { type: 'Point', coordinates: [e.lngLat.lng, e.lngLat.lat] },
            });
            mapboxBridge.destinationPicked(local.x, local.y);
          });
          SdvRouteSnap.loadRoute(token).then(() => {
            if (SdvRouteSnap.isReady()) {
              map.getSource('driving-route').setData(SdvRouteSnap.routeLineFeatureLngLat());
            }
          });
        });
      });

      mapboxBridge.egoPoseChanged.connect((x, y, z, yawRad) => {
        if (!map || !map.isStyleLoaded()) {
          return;
        }
        ensureLayers();
        const pose = resolvePose(x, y, yawRad);
        const center = api.localToLngLat(pose.x, pose.y);
        map.getSource('ego').setData({
          type: 'Feature',
          geometry: { type: 'Point', coordinates: center },
          properties: { bearing: pose.bearing },
        });
        const along = SdvRouteSnap.isReady() ? SdvRouteSnap.nearestDistanceAlongRoute(pose.x, pose.y) : 0;
        const traj = SdvRouteSnap.isReady() ? SdvRouteSnap.aheadTrajectoryFeature(along, 70, 40) : null;
        if (traj) {
          map.getSource('trajectory').setData(traj);
        }
        map.easeTo({
          center,
          bearing: 0,
          pitch: 0,
          zoom: 15,
          duration: 120,
          essential: true,
        });
      });

      mapboxBridge.trajectoryChanged.connect((geoJson) => {
        if (!map || !map.isStyleLoaded() || !geoJson) {
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
          console.warn('minimap trajectory', err);
        }
      });

      mapboxBridge.perceptionChanged.connect((geoJson) => {
        if (!map || !map.isStyleLoaded() || !geoJson) {
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
          console.warn('minimap perception', err);
        }
      });

      mapboxBridge.plannedRouteChanged.connect((geoJson) => {
        if (!map || !map.isStyleLoaded() || !geoJson) {
          return;
        }
        ensureLayers();
        try {
          const feat = JSON.parse(geoJson);
          map.getSource('planned-route').setData(feat);
        } catch (err) {
          console.warn('planned route parse', err);
        }
      });

      mapboxBridge.notifyTokenReady();
    });
  }

  api.bootMini = bootMini;
  return api;
})(window.SdvMapbox || {});
