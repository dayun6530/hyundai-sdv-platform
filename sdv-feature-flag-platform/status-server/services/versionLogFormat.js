'use strict';

/** @returns {string} e.g. v3.0 */
function fleetVersionToken(version) {
  const v = String(version || '').trim();
  if (!v || v === 'unknown') return 'v?';
  return /^v/i.test(v) ? (v[0] === 'V' ? `v${v.slice(1)}` : v) : `v${v}`;
}

/** @returns {string} e.g. AdasFeature_ or AdasFeature_Shadow_ */
function fleetDeployLogPrefix(fleetFeatureLabel, shadow = false) {
  if (fleetFeatureLabel === 'ADAS') {
    return shadow ? 'AdasFeature_Shadow_' : 'AdasFeature_';
  }
  if (fleetFeatureLabel === 'Navigation') {
    return shadow ? 'NavigationFeature_Shadow_' : 'NavigationFeature_';
  }
  if (fleetFeatureLabel === 'Energy') {
    return shadow ? 'EvEnergyFeature_Shadow_' : 'EvEnergyFeature_';
  }
  return shadow ? 'Feature_Shadow_' : 'Feature_';
}

/**
 * Unified [VERSION] log line for car CMD / version_events.log.
 * @param {string} fleetFeatureLabel ADAS | Navigation | Energy
 * @param {string} version
 * @param {boolean} [shadow=false]
 */
const VERSION_LOG_BORDER = '*********************';

function formatSimpleVersionDeployedLog(fleetFeatureLabel, version, shadow = false) {
  const prefix = fleetDeployLogPrefix(fleetFeatureLabel, shadow);
  return `${VERSION_LOG_BORDER}[VERSION] ${prefix}${fleetVersionToken(version)} is deployed!${VERSION_LOG_BORDER}`;
}

module.exports = {
  fleetVersionToken,
  fleetDeployLogPrefix,
  formatSimpleVersionDeployedLog,
};
