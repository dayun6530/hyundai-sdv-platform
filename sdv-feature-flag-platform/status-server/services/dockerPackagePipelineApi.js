const fs = require('fs');
const path = require('path');

const FEATURE_CONFIG = {
  ADAS: {
    packageDomain: 'ADAS',
    buildDomain: 'ADAS',
    featureBinary: 'AdasFeature',
    cppPrefix: 'Adas',
    dockerRepo: 'sdv/adas-feature',
  },
  Navigation: {
    packageDomain: 'Navigation',
    buildDomain: 'Navigation',
    featureBinary: 'NavigationFeature',
    cppPrefix: 'Navigation',
    dockerRepo: 'sdv/navigation-feature',
  },
  Energy: {
    packageDomain: 'Energy',
    buildDomain: 'EvEnergy',
    featureBinary: 'EvEnergyFeature',
    cppPrefix: 'EvEnergy',
    dockerRepo: 'sdv/evenergy-feature',
  },
};

const PIPELINE_STEPS = [
  {
    id: 'new_package',
    label: '3 · Package skeleton',
    hint: 'new_package.sh → deploy/packages/.../manifest.json',
  },
  {
    id: 'build',
    label: '4 · Build image',
    hint: 'build_standalone_demo.sh (cpp must exist in demo_src/)',
  },
  {
    id: 'smoke_test',
    label: '5 · Smoke test',
    hint: 'docker run … | head -8',
  },
  {
    id: 'registry_push',
    label: '6 · Registry push',
    hint: 'start local-registry + tag + push candidate image',
  },
  {
    id: 'push_primary',
    label: '6b · Push primary',
    hint: 'build/tag/push primary_version (bootstrap prerequisite)',
  },
  {
    id: 'register',
    label: '7 · Register + version_map',
    hint: 'register_build_artifact.sh (+ sync slot B, former step 8)',
  },
  {
    id: 'bootstrap',
    label: '11 · Vehicle bootstrap',
    hint: 'ota_vehicle.sh bootstrap (Primary+Shadow same version)',
  },
];

const RUN_ALL_STEP_IDS = [
  'new_package',
  'build',
  'smoke_test',
  'registry_push',
  'register',
];

const CAR_ID_RE = /^car_(10|[1-9])$/;
const DEFAULT_REGISTRY = 'localhost:5000';
const RUN_TIMEOUT_MS = 600_000;

function normalizeVersionNoV(raw) {
  const s = String(raw || '').trim();
  if (!s) return '';
  return s.replace(/^[vV]/, '');
}

function versionTag(raw) {
  const n = normalizeVersionNoV(raw);
  return n ? `v${n}` : '';
}

function bashSingleQuote(s) {
  return `'${String(s).replace(/'/g, `'\\''`)}'`;
}

function resolveFeatureConfig(feature) {
  const key = String(feature || '').trim();
  const cfg = FEATURE_CONFIG[key];
  if (!cfg) {
    throw new Error('feature must be ADAS, Navigation, or Energy');
  }
  return cfg;
}

function artifactDirLower(buildDomain) {
  return String(buildDomain || '').trim().toLowerCase();
}

function buildPipelineCommand(deps, stepId, params) {
  const {
    OTA_MANAGER_ROOT,
    OTA_VEHICLE_SCRIPT,
    toWslPath,
    bashSingleQuote: bq,
  } = deps;
  const cfg = resolveFeatureConfig(params.feature);
  const versionNoV = normalizeVersionNoV(params.version);
  if (!versionNoV) {
    throw new Error('version is required');
  }
  const vTag = `v${versionNoV}`;
  const registry = String(params.registry || DEFAULT_REGISTRY).trim().replace(/\/$/, '');
  const otaWsl = toWslPath(OTA_MANAGER_ROOT);
  const domainLower = artifactDirLower(cfg.buildDomain);
  const localImage = `${cfg.dockerRepo}:${vTag}`;
  const registryImage = `${registry}/${localImage}`;
  const binaryPath = `/app/${cfg.featureBinary}`;

  const cd = `cd ${bq(otaWsl)}`;

  switch (stepId) {
    case 'new_package':
      return `${cd} && ./scripts/new_package.sh --domain ${bq(cfg.packageDomain)} --feature ${bq(cfg.featureBinary)} --version ${bq(versionNoV)}`;

    case 'build':
      return `${cd} && ./scripts/build_standalone_demo.sh --domain ${bq(cfg.buildDomain)} --version ${bq(versionNoV)}`;

    case 'smoke_test':
      return `${cd} && docker run --rm ${bq(localImage)} ${bq(binaryPath)} ${bq(versionNoV)} 2>&1 | head -8`;

    case 'registry_push': {
      const ensureRegistry =
        'docker ps --format "{{.Names}}" | grep -qx local-registry || docker run -d -p 5000:5000 --restart unless-stopped --name local-registry registry:2';
      return `${cd} && ${ensureRegistry} && docker tag ${bq(localImage)} ${bq(registryImage)} && docker push ${bq(registryImage)}`;
    }

    case 'push_primary': {
      const primaryNoV = normalizeVersionNoV(params.primary_version);
      if (!primaryNoV) {
        throw new Error('primary_version is required for push_primary (e.g. v4.0)');
      }
      const primaryTag = `v${primaryNoV}`;
      const primaryLocal = `${cfg.dockerRepo}:${primaryTag}`;
      const primaryRegistry = `${registry}/${primaryLocal}`;
      const buildIfMissing = `if ! docker image inspect ${bq(primaryLocal)} >/dev/null 2>&1; then ./scripts/build_standalone_demo.sh --domain ${bq(cfg.buildDomain)} --version ${bq(primaryNoV)}; fi`;
      const ensureRegistry =
        'docker ps --format "{{.Names}}" | grep -qx local-registry || docker run -d -p 5000:5000 --restart unless-stopped --name local-registry registry:2';
      return `${cd} && ${buildIfMissing} && ${ensureRegistry} && docker tag ${bq(primaryLocal)} ${bq(primaryRegistry)} && docker push ${bq(primaryRegistry)}`;
    }

    case 'register': {
      const sourceRel = `build_artifacts/${domainLower}/${vTag}`;
      return `${cd} && ./scripts/register_build_artifact.sh --domain ${bq(cfg.packageDomain)} --version ${bq(versionNoV)} --feature ${bq(cfg.featureBinary)} --source ${bq(sourceRel)} --registry ${bq(registry)} --sync-version-map-slot B`;
    }

    case 'bootstrap': {
      const carId = String(params.car_id || '').trim();
      if (!CAR_ID_RE.test(carId)) {
        throw new Error('car_id must be car_1 .. car_10 for bootstrap');
      }
      const bootstrapNoV = normalizeVersionNoV(
        params.bootstrap_version || params.primary_version,
      );
      if (!bootstrapNoV) {
        throw new Error('bootstrap_version (or primary_version) is required for bootstrap');
      }
      if (!fs.existsSync(OTA_VEHICLE_SCRIPT)) {
        throw new Error(`ota_vehicle.sh not found at ${OTA_VEHICLE_SCRIPT}`);
      }
      const scriptWsl = toWslPath(OTA_VEHICLE_SCRIPT);
      const bootstrapTag = `v${bootstrapNoV}`;
      const bootstrapImage = `${registry}/${cfg.dockerRepo}:${bootstrapTag}`;
      return `bash ${bq(scriptWsl)} bootstrap --car ${bq(carId)} --domain ${bq(cfg.buildDomain)} --version ${bq(bootstrapNoV)} --image ${bq(bootstrapImage)} --pull missing`;
    }

    default:
      throw new Error(`unknown pipeline step: ${stepId}`);
  }
}

function inspectPipelinePreflight(deps, params) {
  const { OTA_MANAGER_ROOT } = deps;
  const cfg = resolveFeatureConfig(params.feature);
  const versionNoV = normalizeVersionNoV(params.version);
  const vTag = versionNoV ? `v${versionNoV}` : '';
  const cppPath = path.join(
    OTA_MANAGER_ROOT,
    'demo_src',
    `${cfg.cppPrefix}_${vTag}.cpp`,
  );
  const artifactDir = path.join(
    OTA_MANAGER_ROOT,
    'build_artifacts',
    artifactDirLower(cfg.buildDomain),
    vTag,
  );
  const packageDir = path.join(
    OTA_MANAGER_ROOT,
    'platform',
    'deploy',
    'packages',
    cfg.packageDomain,
    vTag,
  );

  return {
    cpp_path: cppPath,
    cpp_exists: Boolean(versionNoV && fs.existsSync(cppPath)),
    artifact_dir: artifactDir,
    artifact_exists: Boolean(versionNoV && fs.existsSync(artifactDir)),
    package_dir: packageDir,
    package_exists: Boolean(versionNoV && fs.existsSync(packageDir)),
    local_image: vTag ? `${cfg.dockerRepo}:${vTag}` : '',
    registry_image: vTag
      ? `${String(params.registry || DEFAULT_REGISTRY).replace(/\/$/, '')}/${cfg.dockerRepo}:${vTag}`
      : '',
  };
}

async function runPipelineStep(deps, stepId, params) {
  const cmd = buildPipelineCommand(deps, stepId, params);
  const started = Date.now();
  const result = await deps.execWslBash(cmd, { timeout: RUN_TIMEOUT_MS });
  const ok = Number(result.code) === 0;
  return {
    ok,
    step: stepId,
    exit_code: result.code,
    stdout: result.stdout || '',
    stderr: result.stderr || '',
    command_preview: cmd,
    duration_ms: Date.now() - started,
  };
}

function registerDockerPackagePipelineRoutes(app, deps) {
  const {
    OTA_MANAGER_ROOT,
    OTA_VEHICLE_SCRIPT,
    DEPLOY_ROOT,
    execWslBash,
    toWslPath,
    onBootstrapSuccess,
  } = deps;

  const pipelineDeps = {
    OTA_MANAGER_ROOT,
    OTA_VEHICLE_SCRIPT,
    DEPLOY_ROOT,
    execWslBash,
    toWslPath,
    bashSingleQuote,
  };

  app.get('/api/docker-pipeline/steps', (_req, res) => {
    res.json({
      ok: true,
      steps: PIPELINE_STEPS,
      run_all_step_ids: RUN_ALL_STEP_IDS,
      features: Object.keys(FEATURE_CONFIG),
      default_registry: DEFAULT_REGISTRY,
      notes: [
        'Steps 2 (cpp) and 9–10 (campaign/policy) stay manual in IDE.',
        'Step 7 includes former step 8 (version_map slot B sync).',
        'After pipeline, use Deployment Campaigns for Validate / Candidate / Promote.',
      ],
    });
  });

  app.get('/api/docker-pipeline/preflight', (req, res) => {
    try {
      const feature = String(req.query.feature || 'Energy').trim();
      const version = String(req.query.version || '').trim();
      const registry = String(req.query.registry || DEFAULT_REGISTRY).trim();
      const preflight = inspectPipelinePreflight(pipelineDeps, {
        feature,
        version,
        registry,
      });
      res.json({
        ok: true,
        feature,
        version: versionTag(version),
        registry,
        preflight,
        scripts_found: {
          new_package: fs.existsSync(
            path.join(OTA_MANAGER_ROOT, 'scripts', 'new_package.sh'),
          ),
          build_standalone: fs.existsSync(
            path.join(OTA_MANAGER_ROOT, 'scripts', 'build_standalone_demo.sh'),
          ),
          register: fs.existsSync(
            path.join(OTA_MANAGER_ROOT, 'scripts', 'register_build_artifact.sh'),
          ),
          ota_vehicle: fs.existsSync(OTA_VEHICLE_SCRIPT),
        },
      });
    } catch (err) {
      res.status(400).json({ ok: false, error: err.message || String(err) });
    }
  });

  app.post('/api/docker-pipeline/run', async (req, res) => {
    try {
      const body = req.body || {};
      const step = String(body.step || '').trim();
      const runAll = body.run_all === true;
      const params = {
        feature: String(body.feature || 'Energy').trim(),
        version: String(body.version || '').trim(),
        registry: String(body.registry || DEFAULT_REGISTRY).trim(),
        car_id: String(body.car_id || 'car_10').trim(),
        primary_version: String(body.primary_version || '').trim(),
        bootstrap_version: String(body.bootstrap_version || body.primary_version || '').trim(),
      };

      if (!params.version) {
        res.status(400).json({ ok: false, error: 'version is required' });
        return;
      }

      const stepIds = runAll
        ? RUN_ALL_STEP_IDS
        : step
          ? [step]
          : [];

      if (!stepIds.length) {
        res.status(400).json({ ok: false, error: 'step is required (or run_all=true)' });
        return;
      }

      const results = [];
      for (const stepId of stepIds) {
        const out = await runPipelineStep(pipelineDeps, stepId, params);
        results.push(out);
        if (!out.ok) break;
      }

      const last = results[results.length - 1] || null;
      const ok = results.every((r) => r.ok);

      if (
        ok &&
        stepIds.includes('bootstrap') &&
        typeof onBootstrapSuccess === 'function'
      ) {
        const cfg = resolveFeatureConfig(params.feature);
        onBootstrapSuccess({
          car_id: params.car_id,
          domain: cfg.buildDomain,
          version: normalizeVersionNoV(params.bootstrap_version || params.primary_version),
        });
      }

      const syncVersionMap = ok && stepIds.includes('register');

      res.json({
        ok,
        step: runAll ? 'run_all' : step,
        results,
        exit_code: last?.exit_code ?? null,
        stdout: last?.stdout ?? '',
        stderr: last?.stderr ?? '',
        command_preview: last?.command_preview ?? '',
        version_map_synced: syncVersionMap,
      });
    } catch (err) {
      res.status(500).json({ ok: false, error: err.message || String(err) });
    }
  });
}

module.exports = {
  registerDockerPackagePipelineRoutes,
  FEATURE_CONFIG,
  PIPELINE_STEPS,
};
