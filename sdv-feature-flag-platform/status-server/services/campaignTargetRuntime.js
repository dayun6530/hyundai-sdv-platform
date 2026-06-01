const fs = require('fs');
const path = require('path');

const CAR_ID_RE = /^car_(10|[1-9])$/;
const REGIONS = new Set(['KR', 'US', 'EU']);

function sortCarIds(ids) {
  return [...ids].sort((a, b) => {
    const na = Number(String(a).replace('car_', ''));
    const nb = Number(String(b).replace('car_', ''));
    return na - nb;
  });
}

function resolveTargetsFromBody(body, fleetVehicles) {
  const vehicleIds = [
    ...new Set(
      (Array.isArray(body?.vehicle_ids) ? body.vehicle_ids : [])
        .map((id) => String(id || '').trim())
        .filter((id) => CAR_ID_RE.test(id)),
    ),
  ];
  const region = String(body?.region || '')
    .trim()
    .toUpperCase();
  const hasRegion = REGIONS.has(region);

  if (hasRegion && vehicleIds.length) {
    const err = new Error('region and vehicle_ids are mutually exclusive');
    err.statusCode = 400;
    throw err;
  }

  if (hasRegion) {
    const resolved = sortCarIds(
      (fleetVehicles || [])
        .filter((v) => String(v.region || '').toUpperCase() === region)
        .map((v) => String(v.car_id)),
    );
    if (!resolved.length) {
      const err = new Error(`no vehicles in fleet registry for region ${region}`);
      err.statusCode = 400;
      throw err;
    }
    return { scope: 'region', region, vehicle_ids: resolved };
  }

  if (vehicleIds.length) {
    return {
      scope: 'vehicle',
      region: null,
      vehicle_ids: sortCarIds(vehicleIds),
    };
  }

  return { scope: '', region: null, vehicle_ids: [] };
}

function writeRuntimeCampaign(deployRoot, baseCampaign, targets) {
  const runtimeDir = path.join(deployRoot, 'campaign_runs', '.runtime');
  fs.mkdirSync(runtimeDir, { recursive: true });
  const campaignId = String(baseCampaign.campaign_id || 'campaign_runtime');
  const fileName = `${campaignId}_runtime.json`;
  const filePath = path.join(runtimeDir, fileName);

  const data = {
    ...baseCampaign,
    target_selector: {
      scope: targets.scope === 'region' ? 'region' : 'vehicle',
      vehicle_ids: targets.vehicle_ids,
      ...(targets.region ? { region: targets.region } : {}),
    },
  };

  fs.writeFileSync(filePath, `${JSON.stringify(data, null, 2)}\n`, 'utf8');
  return {
    fileName,
    relPath: `deploy/campaign_runs/.runtime/${fileName}`,
    targets,
  };
}

function pickCampaignDeployPath({ deployRoot, hit, body, fleetVehicles }) {
  const targets = resolveTargetsFromBody(body || {}, fleetVehicles);
  if (!targets.vehicle_ids.length) {
    return {
      relPath: hit.campaignPath,
      runtime: null,
      runtime_target: null,
    };
  }
  const runtime = writeRuntimeCampaign(deployRoot, hit.data, targets);
  return {
    relPath: runtime.relPath,
    runtime,
    runtime_target: {
      scope: targets.scope,
      region: targets.region,
      vehicle_ids: targets.vehicle_ids,
    },
  };
}

function loadFleetVehiclesFromFile(repoRoot, platformDeployRoot) {
  const candidates = [
    path.join(repoRoot, 'config', 'fleet', 'vehicles.json'),
    path.join(platformDeployRoot, 'config', 'fleet', 'vehicles.json'),
  ];
  for (const fp of candidates) {
    try {
      if (fs.existsSync(fp)) {
        const data = JSON.parse(fs.readFileSync(fp, 'utf8'));
        return Array.isArray(data.vehicles) ? data.vehicles : [];
      }
    } catch {
      // try next path
    }
  }
  return [];
}

module.exports = {
  CAR_ID_RE,
  REGIONS,
  resolveTargetsFromBody,
  pickCampaignDeployPath,
  loadFleetVehiclesFromFile,
  sortCarIds,
};
