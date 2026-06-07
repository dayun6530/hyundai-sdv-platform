/* global QWebChannel, qt */
window.SdvMapbox = (function () {
  let bridge = null;
  let originLng = 127.0276;
  let originLat = 37.4979;

  function yawToBearingDeg(yawRad) {
    return 90 - (yawRad * 180) / Math.PI;
  }

  function setMapOrigin(lng, lat) {
    if (Number.isFinite(lng) && Number.isFinite(lat)) {
      originLng = lng;
      originLat = lat;
    }
  }

  /** Autoware map frame x/y (meters) → Mapbox [lng, lat]. */
  function localToLngLat(x, y) {
    const latRad = (originLat * Math.PI) / 180;
    const mPerDegLat = 111320;
    const mPerDegLng = 111320 * Math.cos(latRad);
    return [originLng + x / mPerDegLng, originLat + y / mPerDegLat];
  }

  /** Mapbox [lng, lat] → local map frame x/y (meters). */
  function lngLatToLocal(lng, lat) {
    const latRad = (originLat * Math.PI) / 180;
    const mPerDegLat = 111320;
    const mPerDegLng = 111320 * Math.cos(latRad);
    return [(lng - originLng) * mPerDegLng, (lat - originLat) * mPerDegLat];
  }

  function bearingDeg(fromLngLat, toLngLat) {
    const lat1 = (fromLngLat[1] * Math.PI) / 180;
    const lat2 = (toLngLat[1] * Math.PI) / 180;
    const dLng = ((toLngLat[0] - fromLngLat[0]) * Math.PI) / 180;
    const y = Math.sin(dLng) * Math.cos(lat2);
    const x = Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLng);
    return ((Math.atan2(y, x) * 180) / Math.PI + 360) % 360;
  }

  function bearingToYawRad(bearingDeg) {
    return ((90 - bearingDeg) * Math.PI) / 180;
  }

  function transformCoord(c) {
    const ll = localToLngLat(c[0], c[1]);
    return c.length > 2 ? [ll[0], ll[1], c[2]] : ll;
  }

  function transformLineFeature(feature) {
    if (!feature || !feature.geometry || feature.geometry.type !== 'LineString') {
      return feature;
    }
    feature.geometry.coordinates = feature.geometry.coordinates.map(transformCoord);
    return feature;
  }

  function transformFeatureCollection(fc) {
    if (!fc || !fc.features) {
      return fc;
    }
    fc.features.forEach((f) => {
      if (f.geometry && f.geometry.type === 'Point') {
        f.geometry.coordinates = transformCoord(f.geometry.coordinates);
      }
    });
    return fc;
  }

  function initBridge() {
    return new Promise((resolve) => {
      if (bridge) {
        resolve(bridge);
        return;
      }
      new QWebChannel(qt.webChannelTransport, (channel) => {
        bridge = channel.objects.mapboxBridge;
        if (bridge && typeof bridge.mapOriginLng === 'function') {
          setMapOrigin(bridge.mapOriginLng(), bridge.mapOriginLat());
        }
        resolve(bridge);
      });
    });
  }

  return {
    initBridge,
    yawToBearingDeg,
    bearingDeg,
    bearingToYawRad,
    localToLngLat,
    lngLatToLocal,
    transformLineFeature,
    transformFeatureCollection,
    mapOrigin: () => [originLng, originLat],
  };
})();
