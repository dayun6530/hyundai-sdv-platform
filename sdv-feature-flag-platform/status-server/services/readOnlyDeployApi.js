const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const { evaluateCandidateStaged } = require('./campaignCandidateStaged');
const {
  loadFleetVehiclesFromFile,
  resolveTargetsFromBody,
} = require('./campaignTargetRuntime');
const {
  clearCandidateLaneOnCar,
  readVehicleRuntimeFeatureRow,
  repairVehicleRuntimeFromFleetState,
} = require('./vehicleRuntimeFleetSync');

const SAFE_ID_RE = /^[a-zA-Z0-9._-]+$/;
const CAR_ID_RE = /^car_(10|[1-9])$/;
const FLEET_FEATURES = ['ADAS', 'Navigation', 'Energy'];
const LOG_ROLES = new Set(['primary', 'candidate']);
const RUN_PHASES = new Set(['candidate', 'promote']);
const RUN_STATUSES = new Set(['success', 'failed', 'partial']);

const FEATURE_CONTAINER_KEY = {
  ADAS: 'adas',
  Navigation: 'navigation',
  Energy: 'evenergy',
};

const FEATURE_CONFIG_PREFIX = {
  ADAS: 'fleet_adas',
  Navigation: 'fleet_navigation',
  Energy: 'fleet_energy',
};

const RUNTIME_DOMAIN_KEY = {
  ADAS: 'adas',
  Navigation: 'navigation',
  Energy: 'evenergy',
};

function clampInt(raw, fallback, min, max) {
  const n = Number(raw);
  if (!Number.isFinite(n)) return fallback;
  return Math.min(max, Math.max(min, Math.trunc(n)));
}

function validateId(id, label = 'id') {
  const s = String(id || '').trim();
  if (!s || !SAFE_ID_RE.test(s)) {
    const err = new Error(`invalid ${label}: must match ${SAFE_ID_RE}`);
    err.statusCode = 400;
    throw err;
  }
  return s;
}

function validateCarId(carId) {
  const s = String(carId || '').trim();
  if (!CAR_ID_RE.test(s)) {
    const err = new Error('car_id must be car_1 .. car_10');
    err.statusCode = 400;
    throw err;
  }
  return s;
}

function validateFeature(feature) {
  const f = String(feature || '').trim();
  if (!FLEET_FEATURES.includes(f)) {
    const err = new Error(
      `feature must be one of: ${FLEET_FEATURES.join(', ')}`,
    );
    err.statusCode = 400;
    throw err;
  }
  return f;
}

function validateLogRole(role) {
  const r = String(role || '')
    .trim()
    .toLowerCase();
  if (!LOG_ROLES.has(r)) {
    const err = new Error('role must be primary or candidate');
    err.statusCode = 400;
    throw err;
  }
  return r;
}

function safeReadJson(filePath) {
  try {
    const raw = fs.readFileSync(filePath, 'utf8');
    const data = JSON.parse(raw);
    return { ok: true, data, error: null };
  } catch (err) {
    return { ok: false, data: null, error: err.message || String(err) };
  }
}

function listJsonFiles(dirPath) {
  if (!fs.existsSync(dirPath)) {
    return [];
  }
  return fs
    .readdirSync(dirPath, { withFileTypes: true })
    .filter((d) => d.isFile() && d.name.endsWith('.json'))
    .map((d) => d.name)
    .sort();
}

function resolveUnderRoot(rootDir, fileName) {
  const base = path.resolve(rootDir);
  const resolved = path.resolve(base, fileName);
  if (resolved !== base && !resolved.startsWith(base + path.sep)) {
    const err = new Error('path traversal rejected');
    err.statusCode = 400;
    throw err;
  }
  return resolved;
}

function summarizeTargetSelector(sel) {
  if (!sel || typeof sel !== 'object') {
    return { scope: '' };
  }
  const out = { scope: String(sel.scope || '') };
  if (Array.isArray(sel.vehicle_ids) && sel.vehicle_ids.length) {
    out.vehicle_ids = sel.vehicle_ids.map(String);
  }
  if (sel.group) out.group = String(sel.group);
  if (sel.region) out.region = String(sel.region);
  if (sel.compatible_models) out.compatible_models = sel.compatible_models;
  if (sel.compatible_regions) out.compatible_regions = sel.compatible_regions;
  if (sel.exclude_car_ids) out.exclude_car_ids = sel.exclude_car_ids;
  return out;
}

function summarizeStages(stages) {
  if (!Array.isArray(stages)) return [];
  return stages.map((s) => ({
    stage_id: s?.stage_id || '',
    stage_name: s?.stage_name || '',
    deploy_action: s?.deploy_action || s?.action || '',
  }));
}

function summarizeDockerRuntime(dr, perVehicle) {
  if (!dr && !perVehicle?.length) return null;
  const first = Array.isArray(perVehicle) ? perVehicle[0] : null;
  return {
    requested: dr?.requested,
    mode: dr?.mode || first?.docker_runtime_mode || '',
    image: dr?.image || first?.docker_runtime_image || '',
    pull_requested: dr?.pull_requested,
    pull_policy: dr?.pull_policy || '',
    result: first?.docker_runtime_result || dr?.result || '',
    action: first?.docker_runtime_action || '',
    container: first?.docker_runtime_container || '',
  };
}

function parseConfigFleetKeys(text, prefix) {
  const out = {};
  if (!text) return out;
  const re = new RegExp(
    `^${prefix}_(version|image|slot|mode|enabled)\\s+(.+)$`,
  );
  for (const line of String(text).split(/\r?\n/)) {
    const t = line.trim();
    if (!t || t.startsWith('#')) continue;
    const m = t.match(re);
    if (m) {
      out[m[1]] = m[2].trim();
    }
  }
  return out;
}

function dockerContainerFor(carId, feature, role) {
  const key = FEATURE_CONTAINER_KEY[feature];
  if (!key) return null;
  const base = `${carId}_agent_${key}`;
  return role === 'candidate' ? `${base}_shadow` : base;
}

function execFilePromise(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    execFile(command, args, options, (error, stdout, stderr) => {
      if (error) {
        error.stdout = stdout;
        error.stderr = stderr;
        reject(error);
        return;
      }
      resolve({ stdout: stdout || '', stderr: stderr || '' });
    });
  });
}

function isWslRuntime() {
  return (
    process.platform === 'linux' &&
    (Boolean(process.env.WSL_DISTRO_NAME) ||
      fs.existsSync('/proc/sys/fs/binfmt_misc/WSLInterop') ||
      fs.existsSync('/run/WSL'))
  );
}

async function dockerLogsTail(container, tail) {
  const args = ['logs', container, '--tail', String(tail)];
  const opts = { maxBuffer: 512 * 1024, timeout: 20_000, windowsHide: true };
  const runDirect = process.platform !== 'win32' || isWslRuntime();
  try {
    if (runDirect) {
      return await execFilePromise('docker', args, opts);
    }
    return await execFilePromise('docker', args, opts);
  } catch (err) {
    if (process.platform === 'win32' && err.code === 'ENOENT') {
      const distro = String(process.env.SDV_WSL_DISTRO || 'Ubuntu').trim();
      return execFilePromise(
        'wsl.exe',
        ['-d', distro, '--', 'docker', ...args],
        opts,
      );
    }
    if (process.platform === 'win32' && (err.code === 1 || err.code === 125)) {
      const distro = String(process.env.SDV_WSL_DISTRO || 'Ubuntu').trim();
      try {
        return await execFilePromise(
          'wsl.exe',
          ['-d', distro, '--', 'docker', ...args],
          opts,
        );
      } catch (wslErr) {
        throw wslErr;
      }
    }
    throw err;
  }
}

function classifyDockerLogError(err) {
  const msg =
    `${err?.stderr || ''}\n${err?.stdout || ''}\n${err?.message || ''}`.toLowerCase();
  if (
    msg.includes('no such container') ||
    (msg.includes('container') && msg.includes('not found')) ||
    msg.includes('error response from daemon')
  ) {
    return 'not_found';
  }
  if (
    msg.includes('cannot connect') ||
    msg.includes('is the docker daemon running')
  ) {
    return 'error';
  }
  return 'error';
}

function parseJsonlTail(filePath, limit, warnings) {
  if (!fs.existsSync(filePath)) {
    warnings.push(`missing ${path.basename(filePath)}`);
    return { parsed: [], raw_lines: [], parse_errors: [], total_lines: 0 };
  }
  const text = fs.readFileSync(filePath, 'utf8');
  const lines = text
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter(Boolean);
  const parse_errors = [];
  const all = [];
  for (let i = 0; i < lines.length; i += 1) {
    try {
      all.push(JSON.parse(lines[i]));
    } catch (err) {
      parse_errors.push({
        line_index: i,
        error: err.message,
        raw: lines[i].slice(0, 200),
      });
    }
  }
  const tail = all.length <= limit ? all : all.slice(-limit);
  const rawStart = Math.max(0, lines.length - limit);
  return {
    parsed: tail,
    raw_lines: lines.slice(rawStart),
    parse_errors,
    total_lines: lines.length,
    parsed_total: all.length,
  };
}

function filterFleetHistoryRecords(records, { car_id, feature, action }) {
  return records.filter((rec) => {
    if (car_id) {
      const touches =
        rec.car_id === car_id ||
        (Array.isArray(rec.targets) && rec.targets.includes(car_id)) ||
        (Array.isArray(rec.success_targets) &&
          rec.success_targets.includes(car_id)) ||
        (Array.isArray(rec.failed_targets) &&
          rec.failed_targets.includes(car_id)) ||
        (Array.isArray(rec.results) &&
          rec.results.some((r) => r && r.car_id === car_id));
      if (!touches) return false;
    }
    if (feature && String(rec.feature || '') !== feature) return false;
    if (action && String(rec.action || '') !== action) return false;
    return true;
  });
}

function sortRunsNewestFirst(runs) {
  return runs.sort((a, b) => {
    const ta = Date.parse(a.started_at || '') || 0;
    const tb = Date.parse(b.started_at || '') || 0;
    if (tb !== ta) return tb - ta;
    return String(b.file_name || '').localeCompare(String(a.file_name || ''));
  });
}

function campaignRunListItem(fileName, data) {
  return {
    run_id: data.run_id || fileName.replace(/\.json$/, ''),
    campaign_id: data.campaign_id || '',
    phase: data.phase || '',
    status: data.status || '',
    started_at: data.started_at || '',
    finished_at: data.finished_at || '',
    aggregate: data.aggregate || null,
    resolved_targets: Array.isArray(data.resolved_targets)
      ? data.resolved_targets
      : [],
    feature: data.feature || '',
    target_version: data.target_version || '',
    docker_runtime: summarizeDockerRuntime(
      data.docker_runtime,
      data.per_vehicle,
    ),
    file_name: fileName,
  };
}

function registerReadOnlyDeployRoutes(app, deps) {
  const {
    DEPLOY_ROOT,
    PLATFORM_DEPLOY_ROOT,
    OTA_VEHICLE_RUNTIME_DIR,
    REPO_ROOT,
    resolveDockerImageAvailability,
  } = deps;

  const CAMPAIGNS_DIR = path.join(DEPLOY_ROOT, 'campaigns');
  const ROLLOUT_POLICIES_DIR = path.join(DEPLOY_ROOT, 'rollout_policies');
  const CAMPAIGN_RUNS_DIR = path.join(DEPLOY_ROOT, 'campaign_runs');
  const FLEET_HISTORY_PATH = path.join(
    DEPLOY_ROOT,
    'history',
    'fleet_history.jsonl',
  );
  const CONFIG_CAR_DIR = path.join(PLATFORM_DEPLOY_ROOT, 'config', 'car');

  function readAllCampaignFiles(warnings, errors) {
    const files = listJsonFiles(CAMPAIGNS_DIR);
    const campaigns = [];
    for (const fileName of files) {
      const fp = resolveUnderRoot(CAMPAIGNS_DIR, fileName);
      const { ok, data, error } = safeReadJson(fp);
      if (!ok) {
        errors.push({ file_name: fileName, error });
        continue;
      }
      campaigns.push({ fileName, data });
    }
    if (!files.length) {
      warnings.push('no campaign JSON files in deploy/campaigns');
    }
    return campaigns;
  }

  function findCampaignByCampaignId(campaignId) {
    const warnings = [];
    const errors = [];
    const all = readAllCampaignFiles(warnings, errors);
    const hit = all.find(
      (c) => String(c.data?.campaign_id || '') === campaignId,
    );
    return { hit, warnings, errors };
  }

  function readAllPolicyFiles(warnings, errors) {
    const files = listJsonFiles(ROLLOUT_POLICIES_DIR);
    const policies = [];
    for (const fileName of files) {
      const fp = resolveUnderRoot(ROLLOUT_POLICIES_DIR, fileName);
      const { ok, data, error } = safeReadJson(fp);
      if (!ok) {
        errors.push({ file_name: fileName, error });
        continue;
      }
      policies.push({ fileName, data });
    }
    if (!files.length) {
      warnings.push('no rollout policy JSON files in deploy/rollout_policies');
    }
    return policies;
  }

  function findPolicyByPolicyId(policyId) {
    const warnings = [];
    const errors = [];
    const all = readAllPolicyFiles(warnings, errors);
    const hit = all.find((p) => String(p.data?.policy_id || '') === policyId);
    return { hit, warnings, errors };
  }

  function readAllRunFiles(warnings, errors) {
    const files = listJsonFiles(CAMPAIGN_RUNS_DIR).filter((n) =>
      n.startsWith('run_'),
    );
    const runs = [];
    for (const fileName of files) {
      const fp = resolveUnderRoot(CAMPAIGN_RUNS_DIR, fileName);
      const { ok, data, error } = safeReadJson(fp);
      if (!ok) {
        errors.push({ file_name: fileName, error });
        continue;
      }
      runs.push({ fileName, data });
    }
    if (!files.length) {
      warnings.push('no campaign run JSON files in deploy/campaign_runs');
    }
    return runs;
  }

  function findRunByRunId(runId) {
    const warnings = [];
    const errors = [];
    const all = readAllRunFiles(warnings, errors);
    let hit = all.find((r) => String(r.data?.run_id || '') === runId);
    if (!hit && runId.endsWith('.json')) {
      hit = all.find((r) => r.fileName === runId);
    }
    if (!hit) {
      const candidateName = runId.endsWith('.json') ? runId : `${runId}.json`;
      if (SAFE_ID_RE.test(runId.replace(/\.json$/, ''))) {
        try {
          const fp = resolveUnderRoot(CAMPAIGN_RUNS_DIR, candidateName);
          if (fs.existsSync(fp)) {
            const parsed = safeReadJson(fp);
            if (parsed.ok) {
              hit = { fileName: candidateName, data: parsed.data };
            }
          }
        } catch (_) {
          /* invalid file name */
        }
      }
    }
    return { hit, warnings, errors };
  }

  function buildRuntimeSummary(carId) {
    const warnings = [];
    const features = {};

    const statePath = path.join(DEPLOY_ROOT, 'state', `${carId}.json`);
    let state = null;
    if (fs.existsSync(statePath)) {
      const parsed = safeReadJson(statePath);
      if (parsed.ok) state = parsed.data;
      else warnings.push(`deploy/state/${carId}.json: ${parsed.error}`);
    } else {
      warnings.push(`missing deploy/state/${carId}.json`);
    }

    const rtPath = path.join(OTA_VEHICLE_RUNTIME_DIR, `${carId}.json`);
    let vehicleRuntime = null;
    if (fs.existsSync(rtPath)) {
      const parsed = safeReadJson(rtPath);
      if (parsed.ok) vehicleRuntime = parsed.data;
      else warnings.push(`vehicle_runtime/${carId}.json: ${parsed.error}`);
    } else {
      warnings.push(`missing vehicle_runtime/${carId}.json`);
    }

    const confPath = path.join(CONFIG_CAR_DIR, `${carId}.conf`);
    let configText = '';
    if (fs.existsSync(confPath)) {
      try {
        configText = fs.readFileSync(confPath, 'utf8');
      } catch (err) {
        warnings.push(`config/car/${carId}.conf: ${err.message}`);
      }
    } else {
      warnings.push(`missing config/car/${carId}.conf`);
    }

    for (const feature of FLEET_FEATURES) {
      const domainKey = RUNTIME_DOMAIN_KEY[feature];
      const cfgPrefix = FEATURE_CONFIG_PREFIX[feature];
      const st = state?.features?.[feature] || {};
      const dom = vehicleRuntime?.domains?.[domainKey] || {};
      const cfg = parseConfigFleetKeys(configText, cfgPrefix);

      const candidateVersion = st.shadow_version || dom.shadow_version || '';
      const candidateImage = st.shadow_image || dom.shadow_image || '';

      features[feature] = {
        primary_version:
          st.primary_version || st.active_version || dom.primary_version || '',
        primary_image: st.primary_image || dom.primary_image || '',
        candidate_version: candidateVersion,
        candidate_image: candidateImage,
        primary_container:
          dom.primary_container || st.docker_runtime_container || '',
        candidate_container: dom.shadow_container || '',
        shadow_enabled: Boolean(st.shadow_enabled ?? dom.shadow_enabled),
        last_action: dom.last_action || '',
        docker_runtime_result: st.docker_runtime_result || '',
        docker_runtime_mode: st.docker_runtime_mode || '',
        docker_runtime_action: st.docker_runtime_action || '',
        package_id: st.package_id || '',
        manifest_path: st.manifest_path || '',
        config_version: cfg.version || '',
        config_image: cfg.image || '',
        config_slot: cfg.slot || '',
        config_mode: cfg.mode || '',
      };
    }

    return { car_id: carId, features, warnings };
  }

  app.get('/api/campaigns', async (_req, res) => {
    const warnings = [];
    const errors = [];
    const all = readAllCampaignFiles(warnings, errors);
    const campaigns = all.map(({ fileName, data }) => ({
      campaign_id: data.campaign_id || '',
      name: data.name || '',
      status: data.status || '',
      feature: data.feature || '',
      target_version: data.target_version || '',
      target_role: data.target_role || '',
      rollout_policy_id: data.rollout_policy_id || '',
      target_selector: summarizeTargetSelector(data.target_selector),
      docker_image: data.docker_image || '',
      approval_required: Boolean(data.approval_required),
      file_name: fileName,
    }));

    if (typeof resolveDockerImageAvailability === 'function') {
      try {
        const imageMap = await resolveDockerImageAvailability(
          campaigns.map((c) => c.docker_image),
        );
        for (const c of campaigns) {
          const tag = c.docker_image;
          c.image_available =
            tag && Object.prototype.hasOwnProperty.call(imageMap, tag)
              ? Boolean(imageMap[tag])
              : undefined;
        }
      } catch (err) {
        warnings.push(`docker image availability: ${err.message}`);
      }
    }

    res.json({ ok: true, campaigns, warnings, errors });
  });

  app.get('/api/campaigns/:campaignId', (req, res) => {
    try {
      const campaignId = validateId(req.params.campaignId, 'campaignId');
      const { hit, warnings, errors } = findCampaignByCampaignId(campaignId);
      if (!hit) {
        res
          .status(404)
          .json({
            ok: false,
            error: `campaign not found: ${campaignId}`,
            warnings,
            errors,
          });
        return;
      }

      let rollout_policy = null;
      const policyId = String(hit.data.rollout_policy_id || '');
      if (policyId) {
        const pol = findPolicyByPolicyId(policyId);
        warnings.push(...pol.warnings);
        errors.push(...pol.errors);
        if (pol.hit) {
          rollout_policy = pol.hit.data;
        } else {
          warnings.push(`rollout policy not found: ${policyId}`);
        }
      }

      const allRuns = readAllRunFiles([], []);
      const latest_runs = sortRunsNewestFirst(
        allRuns
          .filter((r) => String(r.data?.campaign_id || '') === campaignId)
          .map(({ fileName, data }) => campaignRunListItem(fileName, data)),
      ).slice(0, 10);

      res.json({
        ok: true,
        campaign: hit.data,
        campaign_file_name: hit.fileName,
        rollout_policy,
        latest_runs,
        warnings,
        errors,
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.get('/api/campaigns/:campaignId/runs', (req, res) => {
    try {
      const campaignId = validateId(req.params.campaignId, 'campaignId');
      const limit = clampInt(req.query.limit, 20, 1, 100);
      const phase = req.query.phase ? String(req.query.phase).trim() : '';
      const status = req.query.status ? String(req.query.status).trim() : '';
      if (phase && !RUN_PHASES.has(phase)) {
        res
          .status(400)
          .json({ ok: false, error: 'phase must be candidate or promote' });
        return;
      }
      if (status && !RUN_STATUSES.has(status)) {
        res
          .status(400)
          .json({
            ok: false,
            error: 'status must be success, failed, or partial',
          });
        return;
      }

      const warnings = [];
      const errors = [];
      const { hit } = findCampaignByCampaignId(campaignId);
      if (!hit) {
        res
          .status(404)
          .json({ ok: false, error: `campaign not found: ${campaignId}` });
        return;
      }

      let runs = readAllRunFiles(warnings, errors)
        .filter((r) => String(r.data?.campaign_id || '') === campaignId)
        .map(({ fileName, data }) => campaignRunListItem(fileName, data));

      if (phase) runs = runs.filter((r) => r.phase === phase);
      if (status) runs = runs.filter((r) => r.status === status);
      runs = sortRunsNewestFirst(runs).slice(0, limit);

      res.json({
        ok: true,
        campaign_id: campaignId,
        runs,
        limit,
        warnings,
        errors,
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.get('/api/rollout-policies', (_req, res) => {
    const warnings = [];
    const errors = [];
    const all = readAllPolicyFiles(warnings, errors);
    const policies = all.map(({ fileName, data }) => ({
      policy_id: data.policy_id || '',
      name: data.name || '',
      description: data.description || '',
      stages: summarizeStages(data.stages),
      file_name: fileName,
    }));
    res.json({ ok: true, policies, warnings, errors });
  });

  app.get('/api/rollout-policies/:policyId', (req, res) => {
    try {
      const policyId = validateId(req.params.policyId, 'policyId');
      const { hit, warnings, errors } = findPolicyByPolicyId(policyId);
      if (!hit) {
        res
          .status(404)
          .json({
            ok: false,
            error: `rollout policy not found: ${policyId}`,
            warnings,
            errors,
          });
        return;
      }
      res.json({
        ok: true,
        policy: hit.data,
        file_name: hit.fileName,
        warnings,
        errors,
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.get('/api/campaign-runs', (req, res) => {
    const warnings = [];
    const errors = [];
    const limit = clampInt(req.query.limit, 20, 1, 100);
    const campaign_id = req.query.campaign_id
      ? String(req.query.campaign_id).trim()
      : '';
    const phase = req.query.phase ? String(req.query.phase).trim() : '';
    const status = req.query.status ? String(req.query.status).trim() : '';

    if (campaign_id) {
      try {
        validateId(campaign_id, 'campaign_id');
      } catch (err) {
        res.status(400).json({ ok: false, error: err.message });
        return;
      }
    }
    if (phase && !RUN_PHASES.has(phase)) {
      res
        .status(400)
        .json({ ok: false, error: 'phase must be candidate or promote' });
      return;
    }
    if (status && !RUN_STATUSES.has(status)) {
      res
        .status(400)
        .json({
          ok: false,
          error: 'status must be success, failed, or partial',
        });
      return;
    }

    let runs = readAllRunFiles(warnings, errors).map(({ fileName, data }) =>
      campaignRunListItem(fileName, data),
    );
    if (campaign_id) runs = runs.filter((r) => r.campaign_id === campaign_id);
    if (phase) runs = runs.filter((r) => r.phase === phase);
    if (status) runs = runs.filter((r) => r.status === status);
    runs = sortRunsNewestFirst(runs).slice(0, limit);

    res.json({ ok: true, runs, limit, warnings, errors });
  });

  app.get('/api/campaign-runs/:runId', (req, res) => {
    try {
      const runId = validateId(req.params.runId, 'runId');
      const { hit, warnings, errors } = findRunByRunId(runId);
      if (!hit) {
        res
          .status(404)
          .json({
            ok: false,
            error: `campaign run not found: ${runId}`,
            warnings,
            errors,
          });
        return;
      }
      res.json({
        ok: true,
        run: hit.data,
        file_name: hit.fileName,
        warnings,
        errors,
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.get('/api/vehicles/:carId/runtime-summary', (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const summary = buildRuntimeSummary(carId);
      res.json({ ok: true, ...summary });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.post('/api/campaigns/:campaignId/candidate-staged', (req, res) => {
    try {
      const campaignId = validateId(req.params.campaignId, 'campaignId');
      const { hit } = findCampaignByCampaignId(campaignId);
      if (!hit) {
        res
          .status(404)
          .json({ ok: false, error: `campaign not found: ${campaignId}` });
        return;
      }
      const feature = String(hit.data?.feature || '').trim();
      if (!FLEET_FEATURES.includes(feature)) {
        res.status(400).json({
          ok: false,
          error: `campaign feature must be one of: ${FLEET_FEATURES.join(', ')}`,
        });
        return;
      }
      const targetVersion = String(hit.data?.target_version || '').trim();
      if (!targetVersion) {
        res.status(400).json({
          ok: false,
          error: 'campaign missing target_version',
        });
        return;
      }
      const fleetVehicles =
        typeof loadFleetVehiclesFromFile === 'function'
          ? loadFleetVehiclesFromFile(REPO_ROOT, PLATFORM_DEPLOY_ROOT)
          : [];
      const targets = resolveTargetsFromBody(req.body || {}, fleetVehicles);
      if (!targets.vehicle_ids.length) {
        res.status(400).json({
          ok: false,
          error: 'deployment target required (region or vehicle_ids)',
        });
        return;
      }
      const stateDir = path.join(DEPLOY_ROOT, 'state');
      const repairs = [];
      for (const carId of targets.vehicle_ids) {
        const r = repairVehicleRuntimeFromFleetState(
          stateDir,
          OTA_VEHICLE_RUNTIME_DIR,
          carId,
          feature,
        );
        if (r.repaired) repairs.push({ car_id: carId, ...r });
      }

      const readRuntimeRow = (carId, feat) =>
        readVehicleRuntimeFeatureRow(OTA_VEHICLE_RUNTIME_DIR, carId, feat);

      const result = evaluateCandidateStaged(
        readRuntimeRow,
        targets.vehicle_ids,
        feature,
        targetVersion,
      );
      res.json({
        ok: true,
        campaign_id: campaignId,
        resolved_targets: targets.vehicle_ids,
        runtime_repairs: repairs,
        ...result,
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.post('/api/campaigns/:campaignId/drop-candidate', (req, res) => {
    try {
      const campaignId = validateId(req.params.campaignId, 'campaignId');
      const { hit } = findCampaignByCampaignId(campaignId);
      if (!hit) {
        res
          .status(404)
          .json({ ok: false, error: `campaign not found: ${campaignId}` });
        return;
      }
      const feature = String(hit.data?.feature || '').trim();
      if (!FLEET_FEATURES.includes(feature)) {
        res.status(400).json({
          ok: false,
          error: `campaign feature must be one of: ${FLEET_FEATURES.join(', ')}`,
        });
        return;
      }
      const fleetVehicles =
        typeof loadFleetVehiclesFromFile === 'function'
          ? loadFleetVehiclesFromFile(REPO_ROOT, PLATFORM_DEPLOY_ROOT)
          : [];
      const targets = resolveTargetsFromBody(req.body || {}, fleetVehicles);
      if (!targets.vehicle_ids.length) {
        res.status(400).json({
          ok: false,
          error: 'deployment target required (region or vehicle_ids)',
        });
        return;
      }
      const stateDir = path.join(DEPLOY_ROOT, 'state');
      const results = targets.vehicle_ids.map((carId) =>
        clearCandidateLaneOnCar(stateDir, OTA_VEHICLE_RUNTIME_DIR, carId, feature),
      );
      res.json({
        ok: true,
        campaign_id: campaignId,
        feature,
        resolved_targets: targets.vehicle_ids,
        results,
      });
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.get('/api/logs/docker/:carId/:feature/:role', async (req, res) => {
    try {
      const carId = validateCarId(req.params.carId);
      const feature = validateFeature(req.params.feature);
      const role = validateLogRole(req.params.role);
      const tail = clampInt(req.query.tail, 80, 1, 200);
      const container = dockerContainerFor(carId, feature, role);
      if (!container) {
        res
          .status(400)
          .json({ ok: false, error: 'unsupported feature for docker logs' });
        return;
      }

      try {
        const { stdout, stderr } = await dockerLogsTail(container, tail);
        const text = String(stdout || stderr || '').trimEnd();
        const lines = text ? text.split(/\r?\n/) : [];
        res.json({
          ok: true,
          car_id: carId,
          feature,
          role,
          container,
          tail,
          lines,
          text,
          status: 'ok',
        });
      } catch (err) {
        const status = classifyDockerLogError(err);
        res.json({
          ok: status !== 'error',
          car_id: carId,
          feature,
          role,
          container,
          tail,
          lines: [],
          text: String(err.stderr || err.stdout || err.message || '').trimEnd(),
          status,
          error: err.message || String(err),
        });
      }
    } catch (err) {
      res.status(err.statusCode || 500).json({ ok: false, error: err.message });
    }
  });

  app.get('/api/logs/fleet-history', (req, res) => {
    const warnings = [];
    const limit = clampInt(req.query.limit, 30, 1, 100);
    const car_id = req.query.car_id ? String(req.query.car_id).trim() : '';
    const feature = req.query.feature ? String(req.query.feature).trim() : '';
    const action = req.query.action ? String(req.query.action).trim() : '';

    if (car_id && !CAR_ID_RE.test(car_id)) {
      res
        .status(400)
        .json({ ok: false, error: 'car_id must be car_1 .. car_10' });
      return;
    }
    if (feature && !FLEET_FEATURES.includes(feature)) {
      res
        .status(400)
        .json({
          ok: false,
          error: `feature must be one of: ${FLEET_FEATURES.join(', ')}`,
        });
      return;
    }

    const { parsed, raw_lines, parse_errors, total_lines, parsed_total } =
      parseJsonlTail(FLEET_HISTORY_PATH, limit, warnings);
    const filtered = filterFleetHistoryRecords(parsed, {
      car_id,
      feature,
      action,
    });
    res.json({
      ok: true,
      entries: filtered,
      entry_count: filtered.length,
      raw_lines,
      parse_errors,
      total_lines,
      parsed_total,
      limit,
      filters: {
        car_id: car_id || null,
        feature: feature || null,
        action: action || null,
      },
      warnings,
    });
  });
}

module.exports = { registerReadOnlyDeployRoutes };
