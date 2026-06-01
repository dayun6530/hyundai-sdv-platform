const path = require('path');
const {
  findCampaignByCampaignId,
  validateId,
} = require('./campaignValidateApi');
const { repairVehicleRuntimeFromFleetState } = require('./vehicleRuntimeFleetSync');

const RUN_PHASES = new Set(['candidate', 'promote']);
const RUN_TIMEOUT_MS = 300_000;
const runningCampaignIds = new Set();

function parseCampaignRunOutput(stdout, stderr) {
  const items = [];
  let summary = { total: 0, success: 0, failed: 0 };
  let status = 'FAIL';
  let run_record = '';
  let run_id = '';
  let campaign_id = '';
  let phase = 'candidate';

  const lines = `${stdout || ''}\n${stderr || ''}`.split(/\r?\n/);
  for (const raw of lines) {
    const line = raw.trim();
    if (!line) continue;

    const failReason = line.match(
      /^\[CAMPAIGN_RUN\]\s+status=FAIL\s+reason="(.*)"\s*$/,
    );
    if (failReason) {
      items.push({ level: 'FAIL', message: failReason[1] });
      status = 'FAIL';
      continue;
    }

    const statusLine = line.match(
      /^\[CAMPAIGN_RUN\]\s+status=(SUCCESS|FAIL|PARTIAL)\s+total=(\d+)\s+success=(\d+)\s+failed=(\d+)/,
    );
    if (statusLine) {
      const st = statusLine[1];
      status =
        st === 'SUCCESS' ? 'SUCCESS' : st === 'PARTIAL' ? 'PARTIAL' : 'FAIL';
      summary = {
        total: Number(statusLine[2]),
        success: Number(statusLine[3]),
        failed: Number(statusLine[4]),
      };
      continue;
    }

    const saved = line.match(/^\[CAMPAIGN_RUN\]\s+run_record_saved\s+(\S+)/);
    if (saved) {
      run_record = saved[1];
      run_id = path.basename(saved[1], '.json');
      items.push({ level: 'OK', message: `campaign run saved: ${saved[1]}` });
      continue;
    }

    const valErr = line.match(/^\[CAMPAIGN_RUN\]\s+validation_error:\s*(.*)/);
    if (valErr) {
      items.push({ level: 'FAIL', message: valErr[1].trim() });
      continue;
    }

    const warn = line.match(/^\[CAMPAIGN_RUN\]\s+WARN\s+(.*)/);
    if (warn) {
      items.push({ level: 'WARN', message: warn[1].trim() });
      continue;
    }

    const target = line.match(/^\[CAMPAIGN_RUN\]\s+target=(.+)$/);
    if (target) {
      const body = target[1];
      const resultM = body.match(/result=(\S+)/);
      const lvl = resultM && resultM[1] === 'success' ? 'OK' : 'FAIL';
      items.push({ level: lvl, message: body });
      if (body.includes('docker_runtime_result=success')) {
        const isPromote =
          phase === 'promote' ||
          body.includes('docker_runtime=promote') ||
          body.includes('role=primary');
        items.push({
          level: 'OK',
          message: isPromote
            ? 'docker runtime promote success'
            : 'docker runtime candidate success',
        });
      }
      if (body.includes('docker_runtime=promote')) {
        items.push({ level: 'OK', message: 'docker_runtime_mode=promote' });
      }
      continue;
    }

    const meta = line.match(
      /^\[CAMPAIGN_RUN\]\s+campaign_id=(\S+)\s+phase=(\S+)/,
    );
    if (meta) {
      campaign_id = meta[1];
      phase = meta[2];
      items.push({
        level: 'OK',
        message: `campaign_id=${campaign_id} phase=${phase}`,
      });
      continue;
    }

    const runLine = line.match(/^\[CAMPAIGN_RUN\]\s+(.*)/);
    if (runLine && !runLine[1].startsWith('status=')) {
      items.push({ level: 'OK', message: runLine[1].trim() });
    }
  }

  if (
    summary.total === 0 &&
    items.some((i) => i.message.includes('resolved_targets'))
  ) {
    const m = items.find((i) => i.message.includes('resolved_targets'));
    const countM = m && m.message.match(/count=(\d+)/);
    if (countM) summary.total = Number(countM[1]);
  }

  return { summary, items, status, run_record, run_id, campaign_id, phase };
}

function registerCampaignRunRoutes(app, deps) {
  const {
    DEPLOY_ROOT,
    PLATFORM_DEPLOY_ROOT,
    REPO_ROOT,
    execWslBash,
    detectDeploymentMgrPath,
    toWslPath,
    bashSingleQuote,
    syncRuntimeCmdForCars,
    extractCarIdsFromCampaign,
    pickCampaignDeployPath,
    loadFleetVehiclesFromFile,
  } = deps;

  const CAMPAIGNS_DIR = path.join(DEPLOY_ROOT, 'campaigns');

  app.post('/api/campaigns/:campaignId/run', async (req, res) => {
    let campaignId = '';
    let phase = 'candidate';
    try {
      campaignId = validateId(req.params.campaignId, 'campaignId');
      phase = String(req.query.phase || '')
        .trim()
        .toLowerCase();

      if (!RUN_PHASES.has(phase)) {
        res.status(400).json({
          ok: false,
          execution: true,
          error: 'phase must be candidate or promote',
          phase,
        });
        return;
      }

      if (runningCampaignIds.has(campaignId)) {
        res.status(409).json({
          ok: false,
          execution: true,
          campaign_id: campaignId,
          phase,
          status: 'FAIL',
          error: 'campaign run already in progress for this campaign_id',
        });
        return;
      }

      runningCampaignIds.add(campaignId);
      try {
        const hit = findCampaignByCampaignId(CAMPAIGNS_DIR, campaignId);
        if (!hit) {
          res.status(404).json({
            ok: false,
            execution: true,
            error: `campaign not found: ${campaignId}`,
            campaign_id: campaignId,
            phase,
          });
          return;
        }

        let dm;
        try {
          dm = detectDeploymentMgrPath();
        } catch (err) {
          res.status(503).json({
            ok: false,
            execution: true,
            error: err.message,
            campaign_id: campaignId,
            phase,
          });
          return;
        }

        const fleetVehicles =
          typeof loadFleetVehiclesFromFile === 'function'
            ? loadFleetVehiclesFromFile(REPO_ROOT, PLATFORM_DEPLOY_ROOT)
            : [];
        const picked =
          typeof pickCampaignDeployPath === 'function'
            ? pickCampaignDeployPath({
                deployRoot: DEPLOY_ROOT,
                hit,
                body: req.body || {},
                fleetVehicles,
              })
            : { relPath: hit.campaignPath, runtime_target: null };

        const repoWsl = toWslPath(PLATFORM_DEPLOY_ROOT);
        const relPath = picked.relPath;
        const cmd =
          `cd ${bashSingleQuote(repoWsl)} && ` +
          `${bashSingleQuote(dm.wslPath)} campaign-run ${bashSingleQuote(relPath)} --phase ${bashSingleQuote(phase)}`;

        const r = await execWslBash(cmd, { timeout: RUN_TIMEOUT_MS });
        const parsed = parseCampaignRunOutput(r.stdout, r.stderr);
        const exitOk = Number(r.code) === 0 && parsed.status === 'SUCCESS';

        const carIds =
          picked.runtime_target?.vehicle_ids?.length
            ? picked.runtime_target.vehicle_ids
            : typeof extractCarIdsFromCampaign === 'function'
              ? extractCarIdsFromCampaign(hit.data)
              : [];

        if (exitOk && carIds.length) {
          const feature = String(hit.data?.feature || '').trim();
          const stateDir = path.join(DEPLOY_ROOT, 'state');
          const otaRtDir = deps.OTA_VEHICLE_RUNTIME_DIR;
          if (otaRtDir && feature && phase === 'candidate') {
            for (const carId of carIds) {
              repairVehicleRuntimeFromFleetState(
                stateDir,
                otaRtDir,
                carId,
                feature,
              );
            }
          }
          if (typeof syncRuntimeCmdForCars === 'function') {
            syncRuntimeCmdForCars(carIds);
          }
        }

        res.json({
          ok: exitOk,
          execution: true,
          campaign_id: campaignId,
          phase: parsed.phase || phase,
          status: parsed.status,
          summary: parsed.summary,
          items: parsed.items,
          run_record: parsed.run_record,
          run_id: parsed.run_id,
          stdout: r.stdout || '',
          stderr: r.stderr || '',
          exit_code: r.code,
          campaign_file: hit.fileName,
          run_command: `DeploymentMgr campaign-run ${relPath} --phase ${phase}`,
          runtime_target: picked.runtime_target || null,
        });
      } finally {
        runningCampaignIds.delete(campaignId);
      }
    } catch (err) {
      const code = err.statusCode || 500;
      res.status(code).json({
        ok: false,
        execution: true,
        campaign_id: campaignId || undefined,
        phase,
        status: 'FAIL',
        summary: { total: 0, success: 0, failed: 1 },
        items: [{ level: 'FAIL', message: err.message || String(err) }],
        error: err.message || String(err),
      });
    }
  });
}

module.exports = {
  registerCampaignRunRoutes,
  parseCampaignRunOutput,
};
