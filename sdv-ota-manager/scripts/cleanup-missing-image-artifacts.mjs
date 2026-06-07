#!/usr/bin/env node
/**
 * Remove image-missing campaign artifacts; reset deploy/state + vehicle_runtime
 * to lowest kept version per feature.
 */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PLATFORM = path.join(ROOT, 'platform');

const KEEP = {
  ADAS: new Set(['v4.0', 'v7.7']),
  Navigation: new Set(['v3.0', 'v5.5', 'v7.7']),
  Energy: new Set(['v3.0', 'v4.0', 'v5.0', 'v7.7']),
};

const LOWEST = {
  ADAS: 'v4.0',
  Navigation: 'v3.0',
  Energy: 'v3.0',
};

const RUNTIME_DOMAIN = {
  ADAS: 'adas',
  Navigation: 'navigation',
  Energy: 'evenergy',
};

const RUNTIME_BINARY = {
  ADAS: 'AdasFeature',
  Navigation: 'NavigationFeature',
  Energy: 'EvEnergyFeature',
};

function rmrf(p) {
  if (!fs.existsSync(p)) return false;
  fs.rmSync(p, { recursive: true, force: true });
  return true;
}

function readJson(p) {
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

function writeJson(p, data) {
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, `${JSON.stringify(data, null, 2)}\n`, 'utf8');
}

function parseVersionNum(v) {
  const s = String(v || '').replace(/^v/i, '');
  const n = parseFloat(s);
  return Number.isFinite(n) ? n : Number.MAX_VALUE;
}

function lowestVersion(versions) {
  return [...versions].sort((a, b) => parseVersionNum(a) - parseVersionNum(b))[0];
}

function loadManifest(feature, version) {
  const fp = path.join(
    PLATFORM,
    'deploy',
    'packages',
    feature,
    version,
    'manifest.json',
  );
  if (!fs.existsSync(fp)) {
    throw new Error(`missing manifest: ${fp}`);
  }
  return readJson(fp);
}

function deleteCampaignArtifacts() {
  const removed = [];
  const campaignsDir = path.join(PLATFORM, 'deploy', 'campaigns');
  for (const file of fs.readdirSync(campaignsDir)) {
    if (!file.startsWith('campaign_') || !file.endsWith('.json')) continue;
    const m = file.match(/^campaign_(adas|navigation|energy)_v(.+)\.json$/i);
    if (!m) continue;
    const feature =
      m[1].toLowerCase() === 'adas'
        ? 'ADAS'
        : m[1].toLowerCase() === 'navigation'
          ? 'Navigation'
          : 'Energy';
    const ver = `v${m[2]}`;
    if (KEEP[feature].has(ver)) continue;
    fs.unlinkSync(path.join(campaignsDir, file));
    removed.push(file);
  }
  return removed;
}

function deletePackageDirs() {
  const removed = [];
  for (const feature of Object.keys(KEEP)) {
    const base = path.join(PLATFORM, 'deploy', 'packages', feature);
    if (!fs.existsSync(base)) continue;
    for (const dir of fs.readdirSync(base)) {
      const ver = dir.startsWith('v') ? dir : `v${dir}`;
      if (KEEP[feature].has(ver)) continue;
      if (rmrf(path.join(base, dir))) removed.push(`packages/${feature}/${dir}`);
    }
  }
  return removed;
}

function deleteRolloutPolicies() {
  const removed = [];
  const dir = path.join(PLATFORM, 'deploy', 'rollout_policies');
  const keepNames = new Set([
    'README.md',
    'policy_canary_then_stable.json',
    'policy_single_car_candidate_manual_promote_adas.json',
    'policy_single_car_candidate_manual_promote_adas_v4.0.json',
    'policy_single_car_candidate_manual_promote_adas_v7.7.json',
    'policy_single_car_candidate_manual_promote_nav_v3.0.json',
    'policy_single_car_candidate_manual_promote_nav_v5.5.json',
    'policy_single_car_candidate_manual_promote_nav_v7.7.json',
    'policy_single_car_candidate_manual_promote_energy_v3.0.json',
    'policy_single_car_candidate_manual_promote_energy_v4.0.json',
    'policy_single_car_candidate_manual_promote_energy_v5.0.json',
    'policy_single_car_candidate_manual_promote_energy_v7.7.json',
  ]);
  for (const file of fs.readdirSync(dir)) {
    if (keepNames.has(file)) continue;
    if (file.startsWith('policy_') && file.endsWith('.json')) {
      fs.unlinkSync(path.join(dir, file));
      removed.push(file);
    }
  }
  return removed;
}

function deleteDemoSrc() {
  const removed = [];
  const dir = path.join(ROOT, 'demo_src');
  for (const file of fs.readdirSync(dir)) {
    if (!file.endsWith('.cpp')) continue;
    let keep = false;
    for (const [feature, vers] of Object.entries(KEEP)) {
      const prefix =
        feature === 'ADAS'
          ? 'Adas_'
          : feature === 'Navigation'
            ? 'Navigation_'
            : 'EvEnergy_';
      if (!file.startsWith(prefix)) continue;
      const base = file.slice(prefix.length).replace(/\.cpp$/i, '');
      const ver = base.startsWith('v') || base.startsWith('V') ? base : `v${base}`;
      if (vers.has(ver)) keep = true;
    }
    if (!keep) {
      fs.unlinkSync(path.join(dir, file));
      removed.push(file);
    }
  }
  return removed;
}

function deleteBuildArtifacts() {
  const removed = [];
  const map = {
    adas: 'ADAS',
    navigation: 'Navigation',
    evenergy: 'Energy',
  };
  const base = path.join(ROOT, 'build_artifacts');
  for (const [folder, feature] of Object.entries(map)) {
    const p = path.join(base, folder);
    if (!fs.existsSync(p)) continue;
    for (const dir of fs.readdirSync(p)) {
      const ver = dir.startsWith('v') ? dir : `v${dir}`;
      if (KEEP[feature].has(ver)) continue;
      if (rmrf(path.join(p, dir))) removed.push(`build_artifacts/${folder}/${dir}`);
    }
  }
  return removed;
}

function deleteCampaignRuns() {
  const removed = [];
  const runsDir = path.join(PLATFORM, 'deploy', 'campaign_runs');
  for (const file of fs.readdirSync(runsDir)) {
    if (file.startsWith('run_') && file.endsWith('.json')) {
      fs.unlinkSync(path.join(runsDir, file));
      removed.push(`campaign_runs/${file}`);
    }
  }
  const rtDir = path.join(runsDir, '.runtime');
  if (fs.existsSync(rtDir)) {
    for (const file of fs.readdirSync(rtDir)) {
      if (file === '.gitkeep') continue;
      fs.unlinkSync(path.join(rtDir, file));
      removed.push(`campaign_runs/.runtime/${file}`);
    }
  }
  return removed;
}

function buildFeatureState(feature, version, prev) {
  const manifest = loadManifest(feature, version);
  const dockerImage = manifest.docker_image || '';
  const verFolder = version;
  return {
    active_slot: 'B',
    active_version: version,
    primary_slot: 'B',
    primary_version: version,
    primary_image: dockerImage,
    artifact_type: manifest.artifact_type || 'docker_image',
    manifest_path: `deploy/packages/${feature}/${verFolder}/manifest.json`,
    package_id: `${feature}_${version}`,
    docker_runtime_result: 'success',
    docker_runtime_mode: 'promote',
    docker_runtime_action: 'promote',
    docker_runtime_time: new Date().toISOString().slice(0, 19).replace('T', ' '),
    docker_runtime_container: '',
    docker_runtime_image: dockerImage,
    pending_active_slot: '',
    pending_active_version: '',
    flag: prev?.flag || 'ON',
    realtime_route_flag: feature === 'Navigation' ? prev?.realtime_route_flag || 'ON' : undefined,
    charging_recommend_flag:
      feature !== 'ADAS' ? prev?.charging_recommend_flag || 'ON' : undefined,
    advanced_range_flag: feature === 'Energy' ? prev?.advanced_range_flag || 'ON' : undefined,
    shadow_enabled: false,
    shadow_slot: '',
    shadow_version: '',
    shadow_status: 'RUNNING',
    shadow_validation_result: 'UNKNOWN',
    shadow_mode: 'inactive',
    kill_switch: false,
    kill_reason: '',
    fallback_slot: prev?.fallback_slot || 'A',
    fallback_version:
      prev?.fallback_version || 'v1.0',
    rollback_stack: [],
    ...(Array.isArray(prev?.blocked_versions) ? { blocked_versions: [] } : {}),
  };
}

function cleanFeatureObject(obj) {
  const out = { ...obj };
  for (const k of Object.keys(out)) {
    if (out[k] === undefined) delete out[k];
  }
  return out;
}

function resetDeployStates() {
  const stateDir = path.join(PLATFORM, 'deploy', 'state');
  const updated = [];
  for (const file of fs.readdirSync(stateDir)) {
    if (!/^car_\d+\.json$/.test(file)) continue;
    const fp = path.join(stateDir, file);
    const carId = file.replace('.json', '');
    const state = readJson(fp);
    if (!state.features) state.features = {};
    for (const feature of ['ADAS', 'Navigation', 'Energy']) {
      const version = LOWEST[feature];
      const prev = state.features[feature];
      state.features[feature] = cleanFeatureObject(
        buildFeatureState(feature, version, prev),
      );
      if (feature === 'ADAS' || feature === 'Navigation') {
        state.features[feature].docker_runtime_container = `${carId}_agent_${RUNTIME_DOMAIN[feature]}`;
      } else {
        state.features[feature].docker_runtime_container = `${carId}_agent_evenergy`;
      }
    }
    state.last_deploy_feature = '';
    state.last_deploy_version = '';
    state.last_deploy_result = '';
    writeJson(fp, state);
    updated.push(file);
  }
  return updated;
}

function resetVehicleRuntime() {
  const rtDir = path.join(ROOT, 'deploy', 'vehicle_runtime');
  const updated = [];
  for (const file of fs.readdirSync(rtDir)) {
    if (!/^car_\d+\.json$/.test(file)) continue;
    const carId = file.replace('.json', '');
    const fp = path.join(rtDir, file);
    const data = readJson(fp);
    if (!data.domains) data.domains = {};
    for (const feature of ['ADAS', 'Navigation', 'Energy']) {
      const domain = RUNTIME_DOMAIN[feature];
      const version = LOWEST[feature];
      const manifest = loadManifest(feature, version);
      data.domains[domain] = {
        primary_version: version,
        shadow_version: '',
        binary: RUNTIME_BINARY[feature],
        primary_container: `${carId}_agent_${domain === 'evenergy' ? 'evenergy' : domain}`,
        shadow_container: `${carId}_agent_${domain === 'evenergy' ? 'evenergy' : domain}_shadow`,
        last_action: 'reset',
        last_message_ko: `${RUNTIME_BINARY[feature]}: Primary(운영) 실행 버전이 ${version}(으)로 정리되었습니다 (artifact cleanup).`,
        shadow_enabled: false,
        shadow_mode: 'inactive',
        ...(manifest.docker_image ? { primary_image: manifest.docker_image } : {}),
      };
    }
    data.car_id = carId;
    writeJson(fp, data);
    updated.push(file);
  }
  if (fs.existsSync(path.join(rtDir, 'pending_docker_rollout.json'))) {
    writeJson(path.join(rtDir, 'pending_docker_rollout.json'), {
      schema_version: 1,
      queue: [],
    });
  }
  return updated;
}

function updateVersionMap() {
  const fp = path.join(PLATFORM, 'deploy', 'version_map.json');
  const vm = readJson(fp);
  for (const feature of ['ADAS', 'Navigation', 'Energy']) {
    const version = LOWEST[feature];
    const manifest = loadManifest(feature, version);
    if (!vm.features[feature]) vm.features[feature] = {};
    vm.features[feature].B = {
      version,
      role: 'candidate',
      binary:
        feature === 'ADAS'
          ? 'AdasFeature'
          : feature === 'Navigation'
            ? 'NavigationFeature'
            : 'EvEnergyFeature',
      package_id: `${feature}_${version}`,
      package_path: `deploy/packages/${feature}/${version}`,
      docker_image: manifest.docker_image || '',
      artifact_type: manifest.artifact_type || 'docker_image',
    };
  }
  writeJson(fp, vm);
}

const report = {
  campaigns: deleteCampaignArtifacts(),
  packages: deletePackageDirs(),
  policies: deleteRolloutPolicies(),
  demo_src: deleteDemoSrc(),
  build_artifacts: deleteBuildArtifacts(),
  campaign_runs: deleteCampaignRuns(),
  deploy_state: resetDeployStates(),
  vehicle_runtime: resetVehicleRuntime(),
  lowest: LOWEST,
  keep: Object.fromEntries(
    Object.entries(KEEP).map(([k, v]) => [k, [...v].sort()]),
  ),
};

updateVersionMap();
console.log(JSON.stringify(report, null, 2));
