import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  applyPendingNativeForCars,
  fetchCampaignDetail,
  fetchCampaignRun,
  fetchCampaignRuns,
  dropCampaignCandidate,
  fetchCampaignCandidateStaged,
  fetchCampaigns,
  fetchRolloutPolicies,
  fetchVehicleRuntimeSummary,
  prepareAllTargetsDemoReady,
  runCampaignPhase,
  validateCampaign,
} from '../api/campaigns.js';
import { fetchFleetVehicles } from '../api/fleet.js';
import { postVehicleRuntimeApplyPending } from '../api/vehicleRuntime.js';
import {
  buildLatestRunByCampaign,
  campaignStatusClass,
  displayTargetRole,
  deploymentTargetKey,
  findLatestSuccessfulCandidateRun,
  formatDockerRuntimeSummary,
  formatPolicyFlow,
  formatTargetSelector,
  inferSingleVehicleId,
  isPrimaryPromotedRow,
  latestRunLabel,
  runStatusClass,
  stageTitle,
  truncateMiddle,
  versionsMatch,
} from './campaignUiModel.js';
import { RawJsonDetails, StatusBadge } from './campaignShared.jsx';
import CampaignActionBar from './CampaignActionBar.jsx';
import CampaignGroupSelector from './CampaignGroupSelector.jsx';
import CampaignTargetSelector from './CampaignTargetSelector.jsx';
import {
  ALL_CAR_IDS,
  allCarsSelected,
  buildRuntimeTargetBody,
  carsInRegion,
  formatDeployTargetLabel,
  sortCarIds,
} from './campaignDeployTargets.js';
import CampaignDemoFlow from './CampaignDemoFlow.jsx';
import CampaignFinalStateBanner from './CampaignFinalStateBanner.jsx';
import CampaignLogsPanel from './CampaignLogsPanel.jsx';
import CampaignExecutionResultPanel from './CampaignExecutionResultPanel.jsx';
import CampaignSelectedHeader from './CampaignSelectedHeader.jsx';
import CampaignValidateResultPanel from './CampaignValidateResultPanel.jsx';
import VehicleSafetyPreconditionsCard from './VehicleSafetyPreconditionsCard.jsx';
import { isRunSuccess } from './campaignUiModel.js';

function PolicyStagesStepper({ policy }) {
  const stages = policy?.stages;
  if (!Array.isArray(stages) || !stages.length) {
    return (
      <div className="empty-state tiny">No rollout policy stages linked.</div>
    );
  }
  return (
    <div className="campaign-stage-flow">
      {stages.map((stage, idx) => (
        <div key={stage.stage_id || idx} className="campaign-stage-card">
          <div className="campaign-stage-head">
            <strong>{stageTitle(stage, idx)}</strong>
            {stage.required ||
            stage.require_ota_ready ||
            stage.require_candidate_running ? (
              <StatusBadge text="required" className="status-yellow" />
            ) : null}
          </div>
          <ul className="campaign-stage-meta tiny">
            {stage.deploy_action ? (
              <li>deploy: {stage.deploy_action}</li>
            ) : null}
            {stage.action ? <li>action: {stage.action}</li> : null}
            {stage.require_ota_ready ? <li>OTA READY required</li> : null}
            {stage.require_candidate_running ? (
              <li>candidate running required</li>
            ) : null}
            {stage.require_manual_approval_before_next_stage ? (
              <li>manual approval before next stage</li>
            ) : null}
            {stage.rollback_on_failure ? <li>rollback on failure</li> : null}
            {stage.docker_runtime?.enabled ? (
              <li>
                docker runtime · pull:{' '}
                {stage.docker_runtime.pull_policy || 'default'}
              </li>
            ) : null}
            {stage.success_criteria?.expected_log_patterns?.length ? (
              <li>
                candidate logs:{' '}
                {stage.success_criteria.expected_log_patterns.join(' · ')}
              </li>
            ) : null}
            {stage.success_criteria?.primary_log_patterns?.length ? (
              <li>
                primary logs:{' '}
                {stage.success_criteria.primary_log_patterns.join(' · ')}
              </li>
            ) : null}
          </ul>
        </div>
      ))}
    </div>
  );
}

function RuntimeSummaryCard({
  carId,
  feature,
  summary,
  loading,
  error,
  preconditionWarnings,
}) {
  if (!carId) {
    return (
      <div className="card campaign-runtime-card">
        <h3>Vehicle Runtime Summary</h3>
        <p className="tiny subtle">
          Select a single-vehicle campaign to show primary / candidate runtime.
        </p>
      </div>
    );
  }
  if (loading) {
    return (
      <div className="card campaign-runtime-card">
        <h3>Vehicle Runtime — {carId}</h3>
        <p className="tiny subtle">Loading runtime summary…</p>
      </div>
    );
  }
  if (error) {
    return (
      <div className="card campaign-runtime-card">
        <h3>Vehicle Runtime — {carId}</h3>
        <p className="text-error tiny">{error}</p>
      </div>
    );
  }

  const f = summary?.features?.[feature] || summary?.features?.ADAS || {};
  const candidateEnabled = Boolean(f.shadow_enabled);
  const dockerOk =
    String(f.docker_runtime_result || '').toLowerCase() === 'success';
  const configSynced = versionsMatch(f.primary_version, f.config_version);

  return (
    <div className="card campaign-runtime-card">
      <div className="card-header campaign-runtime-head">
        <div>
          <h3>Vehicle Runtime — {carId}</h3>
          <p className="tiny subtle">
            Primary / candidate lanes from deploy state, vehicle runtime, and
            config.
          </p>
        </div>
        {dockerOk ? (
          <StatusBadge text="docker OK" className="status-green" />
        ) : null}
        {f.primary_version ? (
          <StatusBadge
            text={`primary ${f.primary_version}`}
            className="status-green"
          />
        ) : null}
        {configSynced ? (
          <StatusBadge text="Config synced" className="status-green" />
        ) : null}
        {!candidateEnabled ? (
          <StatusBadge text="Candidate inactive" className="status-gray" />
        ) : null}
        {candidateEnabled ? (
          <StatusBadge text="Candidate active" className="status-blue" />
        ) : null}
        {f.last_action ? (
          <StatusBadge
            text={`Last action ${f.last_action}`}
            className="status-blue"
          />
        ) : null}
        {preconditionWarnings?.length ? (
          <StatusBadge text="Precondition warning" className="status-yellow" />
        ) : null}
      </div>

      {preconditionWarnings?.length ? (
        <ul className="warning-list tiny" style={{ marginTop: 10 }}>
          {preconditionWarnings.map((w) => (
            <li key={w}>{w}</li>
          ))}
        </ul>
      ) : null}

      <div className="campaign-runtime-grid">
        <div className="campaign-lane-card campaign-lane-primary">
          <div className="campaign-lane-title">Primary</div>
          <dl className="campaign-kv">
            <dt>Version</dt>
            <dd className="mono">{f.primary_version || '—'}</dd>
            <dt>Image</dt>
            <dd className="mono tiny" title={f.primary_image || ''}>
              {truncateMiddle(f.primary_image, 48) || '—'}
            </dd>
            <dt>Container</dt>
            <dd className="mono tiny">{f.primary_container || '—'}</dd>
          </dl>
        </div>
        <div className="campaign-lane-card campaign-lane-candidate">
          <div className="campaign-lane-title">Candidate</div>
          <dl className="campaign-kv">
            <dt>Candidate</dt>
            <dd>
              {candidateEnabled ? (
                <StatusBadge text="enabled" className="status-blue" />
              ) : (
                <StatusBadge text="inactive" className="status-gray" />
              )}
            </dd>
            <dt>Version</dt>
            <dd className="mono">{f.candidate_version || '—'}</dd>
            <dt>Image</dt>
            <dd className="mono tiny" title={f.candidate_image || ''}>
              {truncateMiddle(f.candidate_image, 48) || '—'}
            </dd>
            <dt>Container</dt>
            <dd className="mono tiny">{f.candidate_container || '—'}</dd>
          </dl>
        </div>
      </div>

      <h4 className="campaign-runtime-subhead">Runtime</h4>
      <dl className="campaign-kv campaign-kv-wide">
        <dt>Last action</dt>
        <dd className="mono">{f.last_action || '—'}</dd>
        <dt>Docker result</dt>
        <dd>
          {f.docker_runtime_result ? (
            <StatusBadge
              text={f.docker_runtime_result}
              className={runStatusClass(f.docker_runtime_result)}
            />
          ) : (
            '—'
          )}
        </dd>
        <dt>Docker mode</dt>
        <dd className="mono">{f.docker_runtime_mode || '—'}</dd>
        <dt>Docker action</dt>
        <dd className="mono">{f.docker_runtime_action || '—'}</dd>
      </dl>

      <h4 className="campaign-runtime-subhead">Config</h4>
      <dl className="campaign-kv campaign-kv-wide">
        <dt>Config version</dt>
        <dd className="mono">{f.config_version || '—'}</dd>
        <dt>Config image</dt>
        <dd className="mono tiny" title={f.config_image || ''}>
          {truncateMiddle(f.config_image, 48) || '—'}
        </dd>
      </dl>

      {summary?.warnings?.length ? (
        <ul className="warning-list tiny" style={{ marginTop: 10 }}>
          {summary.warnings.map((w) => (
            <li key={w}>{w}</li>
          ))}
        </ul>
      ) : null}
    </div>
  );
}

function RunDetailDrawer({ run, loading }) {
  if (loading) return <p className="tiny subtle">Loading run detail…</p>;
  if (!run) return null;
  const agg = run.aggregate || {};
  return (
    <div className="campaign-run-detail">
      <div className="campaign-run-detail-head">
        <strong className="mono tiny">{run.run_id}</strong>
        <StatusBadge text={run.phase || '?'} className="status-blue" />
        <StatusBadge
          text={run.status || '?'}
          className={runStatusClass(run.status)}
        />
      </div>
      <dl className="campaign-kv campaign-kv-wide">
        <dt>Targets</dt>
        <dd>{(run.resolved_targets || []).join(', ') || '—'}</dd>
        <dt>Aggregate</dt>
        <dd>
          {agg.success ?? 0} ok / {agg.failed ?? 0} failed / {agg.total ?? 0}{' '}
          total
        </dd>
      </dl>
      {Array.isArray(run.per_vehicle) && run.per_vehicle.length ? (
        <div className="table-wrap" style={{ marginTop: 8 }}>
          <table className="campaign-runs-table">
            <thead>
              <tr>
                <th>Vehicle</th>
                <th>Result</th>
                <th>Docker</th>
              </tr>
            </thead>
            <tbody>
              {run.per_vehicle.map((pv) => (
                <tr key={pv.car_id}>
                  <td className="mono">{pv.car_id}</td>
                  <td>
                    <StatusBadge
                      text={pv.result || '—'}
                      className={runStatusClass(pv.result)}
                    />
                  </td>
                  <td className="tiny mono">
                    {formatDockerRuntimeSummary({
                      mode: pv.docker_runtime_mode,
                      result: pv.docker_runtime_result,
                      action: pv.docker_runtime_action,
                    })}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : null}
      {run.warnings?.length ? (
        <div style={{ marginTop: 8 }}>
          <div className="label">Warnings</div>
          <ul className="simple-list tiny">
            {run.warnings.map((w, i) => (
              <li key={`w-${i}`}>
                {typeof w === 'string' ? w : JSON.stringify(w)}
              </li>
            ))}
          </ul>
        </div>
      ) : null}
      {run.failures?.length ? (
        <div style={{ marginTop: 8 }}>
          <div className="label">Failures</div>
          <ul className="simple-list tiny text-error">
            {run.failures.map((f, i) => (
              <li key={`f-${i}`}>
                {typeof f === 'string' ? f : JSON.stringify(f)}
              </li>
            ))}
          </ul>
        </div>
      ) : null}
      <p className="tiny subtle" style={{ marginTop: 8 }}>
        Full JSON: open the <strong>Campaign Run JSON</strong> tab in Campaign
        Logs below.
      </p>
    </div>
  );
}

export default function CampaignManagementPanel() {
  const [campaigns, setCampaigns] = useState([]);
  const [latestByCampaign, setLatestByCampaign] = useState(new Map());
  const [policyFlowById, setPolicyFlowById] = useState(new Map());
  const [listLoading, setListLoading] = useState(true);
  const [listError, setListError] = useState('');

  const [selectedCampaignId, setSelectedCampaignId] = useState('');
  const [detail, setDetail] = useState(null);
  const [detailLoading, setDetailLoading] = useState(false);
  const [detailError, setDetailError] = useState('');

  const [runs, setRuns] = useState([]);
  const [runsLoading, setRunsLoading] = useState(false);
  const [runsError, setRunsError] = useState('');

  const [selectedRunId, setSelectedRunId] = useState('');
  const [selectedRun, setSelectedRun] = useState(null);
  const [runDetailLoading, setRunDetailLoading] = useState(false);

  const [runtimeSummary, setRuntimeSummary] = useState(null);
  const [runtimeLoading, setRuntimeLoading] = useState(false);
  const [runtimeError, setRuntimeError] = useState('');

  const [validateCache, setValidateCache] = useState({});
  const [validateLoading, setValidateLoading] = useState(false);
  const [validateError, setValidateError] = useState('');

  const [runCandidateCache, setRunCandidateCache] = useState({});
  const [runCandidateLoading, setRunCandidateLoading] = useState(false);
  const [runCandidateError, setRunCandidateError] = useState('');
  const [promoteCache, setPromoteCache] = useState({});
  const [promoteLoading, setPromoteLoading] = useState(false);
  const [promotePhase, setPromotePhase] = useState('');
  const [promoteCompleteMessage, setPromoteCompleteMessage] = useState('');
  const [promoteError, setPromoteError] = useState('');
  const [candidateStaged, setCandidateStaged] = useState(null);
  const [candidateStagedLoading, setCandidateStagedLoading] = useState(false);
  const [dropCandidateLoading, setDropCandidateLoading] = useState(false);
  const [dropCandidateError, setDropCandidateError] = useState('');
  const [logsRefreshToken, setLogsRefreshToken] = useState(0);
  const [preferredLogsTab, setPreferredLogsTab] = useState('');
  const [deploymentCarIds, setDeploymentCarIds] = useState([]);
  const [deploymentRegion, setDeploymentRegion] = useState('');
  const [fleetVehicles, setFleetVehicles] = useState([]);

  const loadList = useCallback(async () => {
    setListLoading(true);
    setListError('');
    const [campRes, runsRes, polRes] = await Promise.all([
      fetchCampaigns(),
      fetchCampaignRuns({ limit: 100 }),
      fetchRolloutPolicies(),
    ]);
    if (!campRes.ok) {
      setListError(campRes.error || 'Failed to load campaigns');
      setCampaigns([]);
    } else {
      setCampaigns(campRes.campaigns || []);
    }
    if (runsRes.ok) {
      setLatestByCampaign(buildLatestRunByCampaign(runsRes.runs || []));
    }
    if (polRes.ok) {
      const map = new Map();
      for (const p of polRes.policies || []) {
        map.set(p.policy_id, formatPolicyFlow(p.stages));
      }
      setPolicyFlowById(map);
    }
    setListLoading(false);
  }, []);

  const loadDetail = useCallback(async (campaignId) => {
    if (!campaignId) return;
    setDetailLoading(true);
    setDetailError('');
    setRunsLoading(true);
    setRunsError('');
    const [detRes, runsRes] = await Promise.all([
      fetchCampaignDetail(campaignId),
      fetchCampaignRuns({ campaign_id: campaignId, limit: 10 }),
    ]);
    if (!detRes.ok) {
      setDetail(null);
      setDetailError(detRes.error || 'Failed to load campaign detail');
    } else {
      setDetail(detRes);
    }
    if (!runsRes.ok) {
      setRuns([]);
      setRunsError(runsRes.error || 'Failed to load campaign runs');
    } else {
      setRuns(runsRes.runs || []);
    }
    setDetailLoading(false);
    setRunsLoading(false);
  }, []);

  const loadRuntime = useCallback(async (carId) => {
    if (!carId) {
      setRuntimeSummary(null);
      setRuntimeError('');
      return;
    }
    setRuntimeLoading(true);
    setRuntimeError('');
    const res = await fetchVehicleRuntimeSummary(carId);
    if (!res.ok) {
      setRuntimeSummary(null);
      setRuntimeError(res.error || 'Failed to load runtime summary');
    } else {
      setRuntimeSummary(res);
    }
    setRuntimeLoading(false);
  }, []);

  const loadRunDetail = useCallback(async (runId) => {
    if (!runId) {
      setSelectedRun(null);
      return;
    }
    setRunDetailLoading(true);
    const res = await fetchCampaignRun(runId);
    if (res.ok) {
      setSelectedRun(res.run || null);
    } else {
      setSelectedRun(null);
    }
    setRunDetailLoading(false);
  }, []);

  useEffect(() => {
    loadList();
    fetchFleetVehicles()
      .then((res) => setFleetVehicles(res.vehicles || []))
      .catch(() => setFleetVehicles([]));
  }, [loadList]);

  useEffect(() => {
    if (selectedCampaignId || !campaigns.length) return;
    const preferred =
      campaigns.find((c) => c.campaign_id === 'campaign_navigation_v3.0') ||
      campaigns.find((c) => c.campaign_id === 'campaign_adas_v7.7') ||
      campaigns[0];
    if (preferred?.campaign_id) setSelectedCampaignId(preferred.campaign_id);
  }, [campaigns, selectedCampaignId]);

  useEffect(() => {
    if (!selectedCampaignId) return;
    loadDetail(selectedCampaignId);
    setSelectedRunId('');
    setSelectedRun(null);
  }, [selectedCampaignId, loadDetail]);

  const campaign = detail?.campaign;
  const policy = detail?.rollout_policy;
  const feature = campaign?.feature || 'ADAS';

  const listCampaign = useMemo(
    () => campaigns.find((c) => c.campaign_id === selectedCampaignId) || null,
    [campaigns, selectedCampaignId],
  );
  const imageAvailable =
    listCampaign?.image_available === undefined
      ? true
      : Boolean(listCampaign?.image_available);

  const resolvedCarIds = useMemo(() => {
    if (deploymentRegion) {
      return carsInRegion(fleetVehicles, deploymentRegion);
    }
    return sortCarIds(deploymentCarIds);
  }, [deploymentRegion, deploymentCarIds, fleetVehicles]);

  const deploymentTargetBody = useMemo(
    () =>
      buildRuntimeTargetBody({
        carIds: deploymentCarIds,
        region: deploymentRegion,
      }),
    [deploymentCarIds, deploymentRegion],
  );

  const currentDeployTargetKey = useMemo(
    () => deploymentTargetKey(deploymentTargetBody),
    [deploymentTargetBody],
  );

  const deploymentHasTarget = Boolean(
    deploymentRegion || deploymentCarIds.length,
  );

  const deployToLabel = formatDeployTargetLabel({
    carIds: deploymentCarIds,
    region: deploymentRegion,
    vehicles: fleetVehicles,
  });

  const runtimeCarId = useMemo(() => {
    if (resolvedCarIds.length >= 1) return resolvedCarIds[0];
    const sel = campaign?.target_selector || detail?.campaign?.target_selector;
    return inferSingleVehicleId(sel, runs.length ? runs : detail?.latest_runs);
  }, [campaign, detail, runs, resolvedCarIds]);

  useEffect(() => {
    loadRuntime(runtimeCarId);
  }, [runtimeCarId, loadRuntime]);

  useEffect(() => {
    loadRunDetail(selectedRunId);
  }, [selectedRunId, loadRunDetail]);

  const latestForSelected = useMemo(() => {
    if (detail?.latest_runs?.length) return detail.latest_runs[0];
    return latestByCampaign.get(selectedCampaignId) || null;
  }, [detail, latestByCampaign, selectedCampaignId]);

  const validateResultRaw = validateCache[selectedCampaignId] || null;
  const validateResult = useMemo(() => {
    if (!validateResultRaw) return null;
    if (validateResultRaw._deployTargetKey !== currentDeployTargetKey)
      return null;
    return validateResultRaw;
  }, [validateResultRaw, currentDeployTargetKey]);

  const runCandidateResult = runCandidateCache[selectedCampaignId] || null;
  const promoteResult = promoteCache[selectedCampaignId] || null;

  const latestCandidateSuccess = useMemo(() => {
    const fromRuns = findLatestSuccessfulCandidateRun(runs, {
      carIds: resolvedCarIds.length ? resolvedCarIds : null,
      targetVersion: campaign?.target_version,
    });
    if (fromRuns) return fromRuns;
    const cached = runCandidateCache[selectedCampaignId];
    if (
      cached?.ok &&
      cached._deployTargetKey === currentDeployTargetKey &&
      String(cached.phase || '').toLowerCase() === 'candidate' &&
      cached.status === 'SUCCESS' &&
      (!campaign?.target_version ||
        !cached.target_version ||
        versionsMatch(cached.target_version, campaign.target_version))
    ) {
      return { run_id: cached.run_id, phase: 'candidate', status: 'success' };
    }
    return null;
  }, [
    runs,
    runCandidateCache,
    selectedCampaignId,
    resolvedCarIds,
    campaign?.target_version,
    currentDeployTargetKey,
  ]);

  const hasFailedCandidateRun = useMemo(
    () =>
      (runs || []).some(
        (r) =>
          String(r?.phase || '').toLowerCase() === 'candidate' &&
          String(r?.status || '').toLowerCase() === 'failed',
      ),
    [runs],
  );

  const candidateStagedForTarget = useMemo(() => {
    if (!candidateStaged?.ready) return false;
    return candidateStaged._deployTargetKey === currentDeployTargetKey;
  }, [candidateStaged, currentDeployTargetKey]);

  const promoteCandidateReady = useMemo(
    () => Boolean(latestCandidateSuccess) || candidateStagedForTarget,
    [latestCandidateSuccess, candidateStagedForTarget],
  );

  const refreshCandidateStaged = useCallback(async () => {
    if (!selectedCampaignId || !deploymentHasTarget) {
      setCandidateStaged(null);
      return;
    }
    setCandidateStagedLoading(true);
    const res = await fetchCampaignCandidateStaged(
      selectedCampaignId,
      deploymentTargetBody,
    );
    setCandidateStaged({
      ...res,
      _deployTargetKey: currentDeployTargetKey,
    });
    setCandidateStagedLoading(false);
  }, [
    selectedCampaignId,
    deploymentHasTarget,
    deploymentTargetBody,
    currentDeployTargetKey,
  ]);

  useEffect(() => {
    refreshCandidateStaged();
  }, [refreshCandidateStaged]);

  const canRunCandidate = useMemo(() => {
    if (
      !selectedCampaignId ||
      !validateResult ||
      runCandidateLoading ||
      promoteLoading
    )
      return false;
    return (validateResult.summary?.fail ?? 0) === 0;
  }, [selectedCampaignId, validateResult, runCandidateLoading, promoteLoading]);

  const canPromote = useMemo(() => {
    if (
      !selectedCampaignId ||
      !validateResult ||
      promoteLoading ||
      runCandidateLoading
    )
      return false;
    if ((validateResult.summary?.fail ?? 0) > 0) return false;
    return promoteCandidateReady;
  }, [
    selectedCampaignId,
    validateResult,
    promoteLoading,
    runCandidateLoading,
    promoteCandidateReady,
  ]);

  const updateModeWarnings = useMemo(() => {
    const items = validateResult?.items || [];
    return items
      .filter(
        (i) =>
          i.level === 'WARN' &&
          /update_mode|not READY/i.test(String(i.message || '')),
      )
      .map((i) => i.message);
  }, [validateResult]);

  const preconditionWarnings = useMemo(() => {
    const items = validateResult?.items || [];
    return items
      .filter(
        (i) =>
          i.level === 'WARN' &&
          /update_mode|READY|OTA|approval/i.test(String(i.message || '')),
      )
      .map((i) => i.message);
  }, [validateResult]);

  const runValidate = useCallback(async () => {
    if (!selectedCampaignId || !deploymentHasTarget) return;
    setValidateLoading(true);
    setValidateError('');
    const res = await validateCampaign(
      selectedCampaignId,
      deploymentTargetBody,
    );
    setValidateCache((prev) => ({
      ...prev,
      [selectedCampaignId]: {
        ...res,
        _deployTargetKey: currentDeployTargetKey,
      },
    }));
    if (res.error && !res.items?.length) {
      setValidateError(res.error);
    } else {
      setValidateError('');
    }
    setValidateLoading(false);
    await refreshCandidateStaged();
  }, [
    selectedCampaignId,
    deploymentHasTarget,
    deploymentTargetBody,
    currentDeployTargetKey,
    refreshCandidateStaged,
  ]);

  const runCandidate = useCallback(async () => {
    if (!selectedCampaignId || !deploymentHasTarget || !imageAvailable) return;
    const vr = validateCache[selectedCampaignId];
    if (
      !vr ||
      vr._deployTargetKey !== currentDeployTargetKey ||
      (vr.summary?.fail ?? 0) > 0
    )
      return;

    setRunCandidateLoading(true);
    setRunCandidateError('');
    const res = await runCampaignPhase(
      selectedCampaignId,
      'candidate',
      deploymentTargetBody,
    );
    setRunCandidateCache((prev) => ({
      ...prev,
      [selectedCampaignId]: {
        ...res,
        _deployTargetKey: currentDeployTargetKey,
        target_version: campaign?.target_version,
      },
    }));
    if (res.error && !res.items?.length) {
      setRunCandidateError(res.error);
    } else {
      setRunCandidateError('');
    }
    setRunCandidateLoading(false);

    await loadList();
    await loadDetail(selectedCampaignId);
    if (runtimeCarId) {
      await loadRuntime(runtimeCarId);
    }
    setLogsRefreshToken((t) => t + 1);

    if (res.ok && res.run_id) {
      setSelectedRunId(res.run_id);
      setPreferredLogsTab('candidate');
    }
    await refreshCandidateStaged();
  }, [
    selectedCampaignId,
    deploymentHasTarget,
    deploymentTargetBody,
    imageAvailable,
    validateCache,
    currentDeployTargetKey,
    campaign?.target_version,
    loadList,
    loadDetail,
    loadRuntime,
    runtimeCarId,
    refreshCandidateStaged,
  ]);

  const canDropCandidate = useMemo(
    () => Boolean(selectedCampaignId && deploymentHasTarget),
    [selectedCampaignId, deploymentHasTarget],
  );

  const runDropCandidate = useCallback(async () => {
    if (!selectedCampaignId || !deploymentHasTarget) return;
    setDropCandidateLoading(true);
    setDropCandidateError('');
    const res = await dropCampaignCandidate(
      selectedCampaignId,
      deploymentTargetBody,
    );
    if (!res.ok) {
      setDropCandidateError(res.error || 'Drop candidate failed');
    } else {
      setDropCandidateError('');
      setRunCandidateCache((prev) => {
        const next = { ...prev };
        delete next[selectedCampaignId];
        return next;
      });
    }
    setDropCandidateLoading(false);
    await refreshCandidateStaged();
    await loadList();
    await loadDetail(selectedCampaignId);
    if (runtimeCarId) {
      await loadRuntime(runtimeCarId);
    }
    setLogsRefreshToken((t) => t + 1);
  }, [
    selectedCampaignId,
    deploymentHasTarget,
    deploymentTargetBody,
    refreshCandidateStaged,
    loadList,
    loadDetail,
    loadRuntime,
    runtimeCarId,
  ]);

  const waitForPromoteOnTargets = useCallback(
    async (carIds, feat, targetVersion) => {
      const deadline = Date.now() + 120_000;
      while (Date.now() < deadline) {
        const checks = await Promise.all(
          carIds.map(async (carId) => {
            const summary = await fetchVehicleRuntimeSummary(carId);
            if (!summary.ok) return false;
            return isPrimaryPromotedRow(summary.features?.[feat], targetVersion);
          }),
        );
        if (checks.length && checks.every(Boolean)) return true;
        await new Promise((r) => setTimeout(r, 2000));
      }
      return false;
    },
    [],
  );

  const runPromote = useCallback(async () => {
    if (!selectedCampaignId || !deploymentHasTarget || !imageAvailable) return;
    const vr = validateCache[selectedCampaignId];
    if (
      !vr ||
      vr._deployTargetKey !== currentDeployTargetKey ||
      (vr.summary?.fail ?? 0) > 0
    )
      return;
    if (!promoteCandidateReady) return;

    if (resolvedCarIds.length) {
      await prepareAllTargetsDemoReady(resolvedCarIds, feature);
    }

    setPromoteLoading(true);
    setPromotePhase('campaign');
    setPromoteError('');
    setPromoteCompleteMessage('');

    let res;
    try {
      res = await runCampaignPhase(
        selectedCampaignId,
        'promote',
        deploymentTargetBody,
      );
      setPromoteCache((prev) => ({ ...prev, [selectedCampaignId]: res }));
      if (res.error && !res.items?.length) {
        setPromoteError(res.error);
      } else if (!res.ok) {
        setPromoteError(res.error || 'Promote campaign-run failed');
      } else {
        setPromoteError('');
      }

      if (res.ok && resolvedCarIds.length) {
        setPromotePhase('docker');
        try {
          await postVehicleRuntimeApplyPending();
        } catch {
          /* queue may be empty */
        }
        setPromotePhase('apply-native');
        const applyRes = await applyPendingNativeForCars(resolvedCarIds);
        if (applyRes.ok === false) {
          setPromoteError(
            applyRes.error || 'apply-pending-native failed on one or more vehicles',
          );
        }
      }

      if (res.ok && resolvedCarIds.length && campaign?.target_version) {
        setPromotePhase('verify');
        const synced = await waitForPromoteOnTargets(
          resolvedCarIds,
          feature,
          campaign.target_version,
        );
        if (synced) {
          setPromoteCompleteMessage(
            resolvedCarIds.length > 1
              ? `Promote complete — primary ${campaign.target_version} on ${resolvedCarIds.length} vehicles.`
              : `Promote complete — primary ${campaign.target_version} on ${resolvedCarIds[0]}.`,
          );
        } else {
          setPromoteCompleteMessage(
            'Promote command finished; refresh vehicle_runtime if JSON still lags.',
          );
        }
      } else if (res.ok) {
        setPromoteCompleteMessage('Promote campaign-run finished.');
      }

      await loadList();
      await loadDetail(selectedCampaignId);
      if (runtimeCarId) {
        await loadRuntime(runtimeCarId);
      }
      setLogsRefreshToken((t) => t + 1);

      if (res.ok && res.run_id) {
        setSelectedRunId(res.run_id);
        setPreferredLogsTab('primary');
      }
      await refreshCandidateStaged();
    } finally {
      setPromoteLoading(false);
      setPromotePhase('');
    }
  }, [
    selectedCampaignId,
    deploymentHasTarget,
    deploymentTargetBody,
    imageAvailable,
    validateCache,
    currentDeployTargetKey,
    promoteCandidateReady,
    resolvedCarIds,
    feature,
    loadList,
    loadDetail,
    loadRuntime,
    runtimeCarId,
    refreshCandidateStaged,
    waitForPromoteOnTargets,
    campaign?.target_version,
  ]);

  const onSafetyChanged = useCallback(async () => {
    if (runtimeCarId) {
      await loadRuntime(runtimeCarId);
    }
    await refreshCandidateStaged();
  }, [runtimeCarId, loadRuntime, refreshCandidateStaged]);

  useEffect(() => {
    if (!selectedCampaignId) return;
    setValidateCache((prev) => {
      const cur = prev[selectedCampaignId];
      if (!cur || cur._deployTargetKey === currentDeployTargetKey) return prev;
      const next = { ...prev };
      delete next[selectedCampaignId];
      return next;
    });
    setRunCandidateCache((prev) => {
      const cur = prev[selectedCampaignId];
      if (!cur || cur._deployTargetKey === currentDeployTargetKey) return prev;
      const next = { ...prev };
      delete next[selectedCampaignId];
      return next;
    });
    setPromoteCache((prev) => {
      if (!prev[selectedCampaignId]) return prev;
      const next = { ...prev };
      delete next[selectedCampaignId];
      return next;
    });
    setCandidateStaged(null);
    setPromoteCompleteMessage('');
  }, [selectedCampaignId, currentDeployTargetKey]);

  const onSelectCampaign = (id) => {
    setSelectedCampaignId(id);
    setDeploymentCarIds([]);
    setDeploymentRegion('');
    setValidateError('');
    setRunCandidateError('');
    setPromoteError('');
    setPromoteCompleteMessage('');
  };

  const onToggleCar = (carId) => {
    setDeploymentRegion('');
    setDeploymentCarIds((prev) => {
      const set = new Set(prev);
      if (set.has(carId)) set.delete(carId);
      else set.add(carId);
      return sortCarIds([...set]);
    });
  };

  const onToggleAllCars = () => {
    setDeploymentRegion('');
    setDeploymentCarIds((prev) =>
      allCarsSelected(prev) ? [] : [...ALL_CAR_IDS],
    );
  };

  const onSelectRegion = (region) => {
    setDeploymentCarIds([]);
    setDeploymentRegion((prev) => (prev === region ? '' : region));
  };

  const refreshAll = async () => {
    await loadList();
    if (selectedCampaignId) {
      loadDetail(selectedCampaignId);
      if (validateCache[selectedCampaignId]) {
        runValidate();
      }
    }
  };

  const refreshRuns = () => {
    if (selectedCampaignId) loadDetail(selectedCampaignId);
  };

  const policyFlow = campaign?.rollout_policy_id
    ? policyFlowById.get(campaign.rollout_policy_id)
    : '';

  const promoteDemoComplete = useMemo(() => {
    const fromRuns = (runs || []).some(
      (r) =>
        String(r.phase || '').toLowerCase() === 'promote' && isRunSuccess(r),
    );
    return (
      fromRuns ||
      (promoteResult?.ok &&
        String(promoteResult.phase || '').toLowerCase() === 'promote')
    );
  }, [runs, promoteResult]);

  return (
    <div className="campaign-section">
      <div className="card campaign-main-card">
        <div className="card-header">
          <div>
            <h2>Deployment Campaigns</h2>
            <p className="tiny subtle">
              Main demo path: Validate → Run Candidate → Promote Primary.
              Primary / candidate terminology; terminal optional.
            </p>
          </div>
          <div className="campaign-header-actions">
            <button
              type="button"
              className="btn"
              onClick={refreshAll}
              disabled={listLoading}
            >
              {listLoading ? 'Refreshing…' : 'Refresh'}
            </button>
          </div>
        </div>

        {listError ? (
          <div className="error-banner tiny">{listError}</div>
        ) : null}

        <CampaignDemoFlow />

        <div className="table-wrap campaign-list-wrap">
          <table className="campaign-list-table">
            <thead>
              <tr>
                <th>Campaign</th>
                <th>Feature</th>
                <th>Version</th>
                <th>Target</th>
                <th>Policy flow</th>
                <th>Docker image</th>
                <th>Status</th>
                <th>Latest run</th>
              </tr>
            </thead>
            <tbody>
              {listLoading && !campaigns.length ? (
                <tr>
                  <td colSpan={8} className="tiny subtle">
                    Loading campaigns…
                  </td>
                </tr>
              ) : null}
              {!listLoading && !campaigns.length ? (
                <tr>
                  <td colSpan={8} className="empty-state tiny">
                    No campaigns found.
                  </td>
                </tr>
              ) : null}
              {campaigns.map((c) => {
                const latest = latestByCampaign.get(c.campaign_id);
                const lr = latestRunLabel(latest);
                const selected = c.campaign_id === selectedCampaignId;
                return (
                  <tr
                    key={c.campaign_id}
                    className={
                      selected
                        ? 'campaign-row-selected'
                        : 'campaign-row-clickable'
                    }
                    onClick={() => onSelectCampaign(c.campaign_id)}
                  >
                    <td>
                      <div className="campaign-name-cell">
                        <strong className="mono tiny">{c.campaign_id}</strong>
                        <span className="tiny subtle">{c.name}</span>
                      </div>
                    </td>
                    <td>{c.feature}</td>
                    <td className="mono">{c.target_version}</td>
                    <td className="tiny">
                      {formatTargetSelector(c.target_selector)}
                    </td>
                    <td className="tiny" title={c.rollout_policy_id || ''}>
                      {policyFlowById.get(c.rollout_policy_id) ||
                        truncateMiddle(c.rollout_policy_id, 28) ||
                        '—'}
                    </td>
                    <td className="mono tiny" title={c.docker_image || ''}>
                      <div className="campaign-docker-image-cell">
                        {truncateMiddle(c.docker_image, 36)}
                        {typeof c.image_available === 'boolean' ? (
                          <StatusBadge
                            text={
                              c.image_available
                                ? 'local image OK'
                                : 'image missing'
                            }
                            className={
                              c.image_available
                                ? 'status-green'
                                : 'status-gray'
                            }
                          />
                        ) : null}
                      </div>
                    </td>
                    <td>
                      <StatusBadge
                        text={c.status || '—'}
                        className={campaignStatusClass(c.status)}
                      />
                    </td>
                    <td>
                      <StatusBadge text={lr.text} className={lr.className} />
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>

      {selectedCampaignId ? (
        <div className="campaign-detail-flow">
          <CampaignSelectedHeader
            campaign={campaign}
            policy={policy}
            policyFlow={policyFlow}
            deployToLabel={deployToLabel}
            imageAvailable={listCampaign?.image_available}
          />

          <CampaignTargetSelector
            selectedCarIds={deploymentCarIds}
            onToggleCar={onToggleCar}
            onToggleAll={onToggleAllCars}
            disabled={!selectedCampaignId}
            regionActive={Boolean(deploymentRegion)}
          />

          <CampaignGroupSelector
            selectedRegion={deploymentRegion}
            onSelectRegion={onSelectRegion}
            vehicles={fleetVehicles}
            disabled={!selectedCampaignId}
            vehiclesDisabled={deploymentCarIds.length > 0}
          />

          {deploymentHasTarget && resolvedCarIds.length > 1 ? (
            <p className="tiny subtle campaign-multi-target-hint">
              Multi-target deploy: Validate checks all selected vehicles (
              {resolvedCarIds.length}).
            </p>
          ) : null}

          <CampaignActionBar
            selectedCampaignId={selectedCampaignId}
            onValidate={runValidate}
            validateLoading={validateLoading}
            onRunCandidate={runCandidate}
            runCandidateLoading={runCandidateLoading}
            canRunCandidate={canRunCandidate}
            onPromote={runPromote}
            promoteLoading={promoteLoading}
            promotePhase={promotePhase}
            promoteCompleteMessage={promoteCompleteMessage}
            canPromote={canPromote}
            validateResult={validateResult}
            updateModeWarnings={updateModeWarnings}
            hasFailedCandidateRun={hasFailedCandidateRun}
            deploymentHasTarget={deploymentHasTarget}
            targetVehicleCount={resolvedCarIds.length}
            imageAvailable={imageAvailable}
            candidateStagedInRuntime={
              candidateStagedForTarget && !latestCandidateSuccess
            }
            onDropCandidate={runDropCandidate}
            dropCandidateLoading={dropCandidateLoading}
            canDropCandidate={canDropCandidate}
          />

          {dropCandidateError ? (
            <p className="tiny campaign-logs-alert campaign-logs-alert-fail">
              {dropCandidateError}
            </p>
          ) : null}

          {promoteCompleteMessage && !promoteLoading ? (
            <p className="tiny campaign-logs-alert campaign-promote-complete-msg">
              {promoteCompleteMessage}
            </p>
          ) : null}

          <VehicleSafetyPreconditionsCard
            carId={runtimeCarId}
            feature={feature}
            onChanged={onSafetyChanged}
          />

          <CampaignFinalStateBanner
            runs={runs}
            runtimeSummary={runtimeSummary}
            feature={feature}
            promoteResult={promoteResult}
          />

          <section className="campaign-results-stack">
            {promoteDemoComplete ? (
              <details className="campaign-result-collapsed">
                <summary>Validate result</summary>
                <CampaignValidateResultPanel
                  result={validateResult}
                  error={validateError}
                  loading={validateLoading}
                />
              </details>
            ) : (
              <CampaignValidateResultPanel
                result={validateResult}
                error={validateError}
                loading={validateLoading}
              />
            )}

            {(runCandidateResult || runCandidateLoading) && (
              <CampaignExecutionResultPanel
                title="Run Candidate Result"
                subtitle="campaign-run --phase candidate"
                mode="run"
                runPhase="candidate"
                loadingLabel="Running candidate phase…"
                result={runCandidateResult}
                error={runCandidateError}
                loading={runCandidateLoading}
              />
            )}

            {(promoteResult || promoteLoading) && (
              <CampaignExecutionResultPanel
                title="Promote Primary Result"
                subtitle="campaign-run --phase promote"
                mode="run"
                runPhase="promote"
                loadingLabel="Promoting candidate to primary…"
                result={promoteResult}
                error={promoteError}
                loading={promoteLoading}
              />
            )}
          </section>

          <RuntimeSummaryCard
            carId={runtimeCarId}
            feature={feature}
            summary={runtimeSummary}
            loading={runtimeLoading}
            error={runtimeError}
            preconditionWarnings={preconditionWarnings}
          />

          <CampaignLogsPanel
            carId={runtimeCarId}
            feature={feature}
            selectedRunId={selectedRunId}
            selectedRun={selectedRun}
            runDetailLoading={runDetailLoading}
            refreshToken={logsRefreshToken}
            preferredTab={preferredLogsTab}
            onPreferredTabApplied={() => setPreferredLogsTab('')}
          />

          <div className="card campaign-runs-card">
            <div className="card-header">
              <h3>Campaign Run History</h3>
              <button
                type="button"
                className="btn"
                onClick={refreshRuns}
                disabled={runsLoading}
              >
                Refresh runs
              </button>
            </div>
            {runsError ? <p className="text-error tiny">{runsError}</p> : null}
            {runsLoading ? <p className="tiny subtle">Loading runs…</p> : null}
            <div className="table-wrap">
              <table className="campaign-runs-table">
                <thead>
                  <tr>
                    <th>Run</th>
                    <th>Phase</th>
                    <th>Status</th>
                    <th>Started</th>
                    <th>Finished</th>
                    <th>Targets</th>
                    <th>OK / Fail</th>
                    <th>Docker</th>
                  </tr>
                </thead>
                <tbody>
                  {!runs.length && !runsLoading ? (
                    <tr>
                      <td colSpan={8} className="tiny subtle">
                        No runs recorded.
                      </td>
                    </tr>
                  ) : null}
                  {runs.map((run) => {
                    const agg = run.aggregate || {};
                    const selected = run.run_id === selectedRunId;
                    return (
                      <tr
                        key={run.run_id}
                        className={
                          selected
                            ? 'campaign-row-selected'
                            : 'campaign-row-clickable'
                        }
                        onClick={() => setSelectedRunId(run.run_id)}
                      >
                        <td className="mono tiny">
                          {truncateMiddle(run.run_id, 32)}
                        </td>
                        <td>
                          <StatusBadge
                            text={run.phase}
                            className="status-blue"
                          />
                        </td>
                        <td>
                          <StatusBadge
                            text={run.status}
                            className={runStatusClass(run.status)}
                          />
                        </td>
                        <td className="tiny mono">{run.started_at || '—'}</td>
                        <td className="tiny mono">{run.finished_at || '—'}</td>
                        <td className="tiny">
                          {(run.resolved_targets || []).join(', ')}
                        </td>
                        <td className="tiny mono">
                          {agg.success ?? 0}/{agg.failed ?? 0}
                        </td>
                        <td
                          className="tiny"
                          title={run.docker_runtime?.image || ''}
                        >
                          {formatDockerRuntimeSummary(run.docker_runtime)}
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
            <RunDetailDrawer run={selectedRun} loading={runDetailLoading} />
          </div>

          <details className="card campaign-advanced-details">
            <summary className="campaign-advanced-summary">
              Advanced · campaign JSON &amp; rollout policy
            </summary>
            <div className="campaign-detail-grid campaign-advanced-inner">
              <div className="card campaign-detail-card">
                <div className="card-header">
                  <div>
                    <h3>Campaign Detail</h3>
                    <p className="mono tiny subtle">{selectedCampaignId}</p>
                  </div>
                  <button
                    type="button"
                    className="btn"
                    onClick={refreshRuns}
                    disabled={runsLoading}
                  >
                    Refresh runs
                  </button>
                </div>

                {detailLoading ? (
                  <p className="tiny subtle">Loading detail…</p>
                ) : null}
                {detailError ? (
                  <p className="text-error tiny">{detailError}</p>
                ) : null}

                {campaign ? (
                  <>
                    <div className="campaign-detail-summary">
                      <h4>{campaign.name}</h4>
                      <p className="tiny subtle">{campaign.description}</p>
                      <div className="vd-badge-row" style={{ marginTop: 10 }}>
                        <StatusBadge
                          text={campaign.feature}
                          className="status-blue"
                        />
                        <StatusBadge
                          text={`target ${campaign.target_version}`}
                          className="status-green"
                        />
                        <StatusBadge
                          text={`role ${displayTargetRole(campaign.target_role)}`}
                          className="status-blue"
                        />
                        <StatusBadge
                          text={
                            campaign.approval_required
                              ? 'approval required'
                              : 'no approval'
                          }
                          className={
                            campaign.approval_required
                              ? 'status-yellow'
                              : 'status-gray'
                          }
                        />
                        {campaign.legacy_slot ? (
                          <span
                            className="tiny subtle"
                            title={`Advanced: internal fleet-deploy slot ${campaign.legacy_slot} maps to candidate`}
                          >
                            slot map
                          </span>
                        ) : null}
                      </div>
                    </div>

                    <dl className="campaign-kv campaign-kv-wide">
                      <dt>Target selector</dt>
                      <dd>{formatTargetSelector(campaign.target_selector)}</dd>
                      <dt>Docker image</dt>
                      <dd
                        className="mono tiny"
                        title={campaign.docker_image || ''}
                      >
                        {campaign.docker_image || '—'}
                      </dd>
                      <dt>Package</dt>
                      <dd className="mono tiny">
                        {campaign.package_id || '—'}
                      </dd>
                    </dl>

                    <details className="campaign-raw-details">
                      <summary>Paths &amp; execution options</summary>
                      <dl className="campaign-kv campaign-kv-wide">
                        <dt>Manifest</dt>
                        <dd className="mono tiny">
                          {campaign.manifest_path || '—'}
                        </dd>
                        <dt>Package path</dt>
                        <dd className="mono tiny">
                          {campaign.package_path || '—'}
                        </dd>
                        <dt>sync_config</dt>
                        <dd>
                          {campaign.execution_options?.sync_config
                            ? 'yes'
                            : 'no'}
                        </dd>
                        <dt>no_defer_native</dt>
                        <dd>
                          {campaign.execution_options?.no_defer_native
                            ? 'yes'
                            : 'no'}
                        </dd>
                      </dl>
                    </details>

                    <details className="campaign-raw-details">
                      <summary>Docker runtime (candidate / promote)</summary>
                      <pre className="json-box mono tiny">
                        {JSON.stringify(campaign.docker_runtime || {}, null, 2)}
                      </pre>
                    </details>

                    {latestForSelected ? (
                      <div className="campaign-latest-run-banner">
                        <span className="label">Latest run</span>
                        <StatusBadge
                          text={latestForSelected.phase}
                          className="status-blue"
                        />
                        <StatusBadge
                          text={latestForSelected.status}
                          className={runStatusClass(latestForSelected.status)}
                        />
                        <span className="tiny mono subtle">
                          {latestForSelected.started_at}
                        </span>
                        {latestForSelected.docker_runtime?.result ===
                          'success' ||
                        latestForSelected.status === 'success' ? (
                          <StatusBadge
                            text="docker success"
                            className="status-green"
                          />
                        ) : null}
                      </div>
                    ) : null}

                    <RawJsonDetails label="Raw campaign JSON" data={campaign} />
                  </>
                ) : null}
              </div>

              <div className="card campaign-policy-card">
                <h3>Rollout Policy</h3>
                {policy ? (
                  <>
                    <p className="mono tiny">{policy.policy_id}</p>
                    <p className="tiny subtle">{policy.name}</p>
                    <p className="tiny subtle">{policy.description}</p>
                    <PolicyStagesStepper policy={policy} />
                    <RawJsonDetails label="Raw policy JSON" data={policy} />
                  </>
                ) : (
                  <p className="tiny subtle">
                    No linked rollout policy loaded.
                  </p>
                )}
              </div>
            </div>
          </details>
        </div>
      ) : (
        <div className="card campaign-detail-card">
          <p className="tiny subtle">
            Select a campaign row to view detail, policy stages, runs, and
            vehicle runtime.
          </p>
        </div>
      )}
    </div>
  );
}
