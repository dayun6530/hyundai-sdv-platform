import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  applyPendingNativeForCars,
  fetchCampaignDetail,
  fetchCampaignRun,
  fetchCampaignRuns,
  dropCampaignCandidate,
  fetchCampaignCandidateStaged,
  fetchCampaigns,
  fetchRolloutPolicies,
  fetchStalePendingWarnings,
  fetchVehicleRuntimeSummary,
  prepareAllTargetsDemoReady,
  runCampaignPhase,
  syncCampaignVersionMap,
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
import CampaignModelSelector from './CampaignModelSelector.jsx';
import CampaignRolloutGroupSelector from './CampaignRolloutGroupSelector.jsx';
import CampaignPercentDeployCard from './CampaignPercentDeployCard.jsx';
import CampaignTargetSelector from './CampaignTargetSelector.jsx';
import {
  aggregateValidateResults,
  allocateCarsByPercent,
  buildPercentDeployKey,
  defaultPercentRows,
  enrichPercentRows,
  isPercentConfigValid,
} from './campaignPercentDeploy.js';
import {
  ALL_CAR_IDS,
  FLEET_MODELS,
  FLEET_REGIONS,
  FLEET_ROLLOUT_GROUPS,
  allCarsSelected,
  buildRuntimeTargetBody,
  computeRolloutGroupProgress,
  formatDeployTargetLabel,
  nextRolloutGroupAfter,
  resolveDeploymentCarIds,
  sortCarIds,
} from './campaignDeployTargets.js';
import CampaignDemoFlow from './CampaignDemoFlow.jsx';
import CampaignFinalStateBanner from './CampaignFinalStateBanner.jsx';
import CampaignLogsPanel from './CampaignLogsPanel.jsx';
import CampaignExecutionResultPanel from './CampaignExecutionResultPanel.jsx';
import CampaignSelectedHeader from './CampaignSelectedHeader.jsx';
import CampaignValidateResultPanel from './CampaignValidateResultPanel.jsx';
import { isRunSuccess } from './campaignUiModel.js';

function percentCandidateAsRunResult(percentRunCandidateResult) {
  if (!percentRunCandidateResult) return null;
  return {
    ok: percentRunCandidateResult.ok,
    status: percentRunCandidateResult.ok ? 'SUCCESS' : 'FAIL',
    phase: 'candidate',
    items: (percentRunCandidateResult.segments || []).flatMap((s) =>
      (s.res?.items || []).map((item) => ({
        ...item,
        message: `[${s.feature} ${s.version}] ${item.message || ''}`,
      })),
    ),
  };
}

function percentPromoteAsRunResult(percentPromoteResult) {
  if (!percentPromoteResult) return null;
  return {
    ok: percentPromoteResult.ok,
    status: percentPromoteResult.ok ? 'SUCCESS' : 'FAIL',
    phase: 'promote',
    items: (percentPromoteResult.segments || []).map((s) => ({
      level: s.res?.ok ? 'OK' : 'FAIL',
      message: `${s.feature} ${s.version} → ${(s.carIds || []).join(', ')}`,
    })),
  };
}

const RUNS_PAGE_SIZE = 10;

function CampaignRunHistoryTable({
  runs,
  runsLoading,
  runsError,
  selectedRunId,
  onSelectRunId,
  onRefresh,
  selectedRun,
  runDetailLoading,
  onCloseRunDetail,
  runsPage,
  onRunsPageChange,
}) {
  const totalRuns = runs?.length || 0;
  const totalPages = Math.max(1, Math.ceil(totalRuns / RUNS_PAGE_SIZE));
  const safePage = Math.min(Math.max(1, runsPage), totalPages);
  const pageStart = (safePage - 1) * RUNS_PAGE_SIZE;
  const paginatedRuns = (runs || []).slice(
    pageStart,
    pageStart + RUNS_PAGE_SIZE,
  );

  return (
    <div className="card campaign-runs-card">
      <div className="card-header">
        <h3>Campaign Run History</h3>
        <button
          type="button"
          className="btn"
          onClick={onRefresh}
          disabled={runsLoading}
        >
          Refresh runs
        </button>
      </div>
      {runsError ? <p className="text-error tiny">{runsError}</p> : null}
      {runsLoading ? <p className="tiny subtle">Loading runs…</p> : null}
      {totalRuns > RUNS_PAGE_SIZE ? (
        <p className="tiny subtle campaign-runs-page-summary">
          Showing {pageStart + 1}–{Math.min(pageStart + RUNS_PAGE_SIZE, totalRuns)}{' '}
          of {totalRuns} runs
        </p>
      ) : null}
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
            {!totalRuns && !runsLoading ? (
              <tr>
                <td colSpan={8} className="tiny subtle">
                  No runs recorded.
                </td>
              </tr>
            ) : null}
            {paginatedRuns.map((run) => {
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
                  onClick={() =>
                    onSelectRunId(selected ? '' : run.run_id)
                  }
                >
                  <td className="mono tiny">
                    {truncateMiddle(run.run_id, 32)}
                  </td>
                  <td>
                    <StatusBadge text={run.phase} className="status-blue" />
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
      {totalPages > 1 ? (
        <div className="campaign-runs-pagination">
          <button
            type="button"
            className="btn btn-secondary"
            disabled={safePage <= 1 || runsLoading}
            onClick={() => onRunsPageChange(safePage - 1)}
          >
            Previous
          </button>
          <span className="tiny subtle campaign-runs-page-indicator">
            Page {safePage} of {totalPages}
          </span>
          <button
            type="button"
            className="btn btn-secondary"
            disabled={safePage >= totalPages || runsLoading}
            onClick={() => onRunsPageChange(safePage + 1)}
          >
            Next
          </button>
        </div>
      ) : null}
      {selectedRunId ? (
        <RunDetailDrawer
          run={selectedRun}
          loading={runDetailLoading}
          onClose={onCloseRunDetail}
        />
      ) : null}
    </div>
  );
}

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
  carOptions,
  onSelectCarId,
  feature,
  summary,
  loading,
  error,
  preconditionWarnings,
}) {
  const [pickerOpen, setPickerOpen] = useState(false);
  const pickerRef = useRef(null);

  useEffect(() => {
    if (!pickerOpen) return undefined;
    const onDoc = (e) => {
      if (pickerRef.current && !pickerRef.current.contains(e.target)) {
        setPickerOpen(false);
      }
    };
    document.addEventListener('mousedown', onDoc);
    return () => document.removeEventListener('mousedown', onDoc);
  }, [pickerOpen]);

  const f = summary?.features?.[feature] || summary?.features?.ADAS || {};
  const candidateEnabled = Boolean(f.shadow_enabled);
  const dockerOk =
    String(f.docker_runtime_result || '').toLowerCase() === 'success';
  const configSynced = versionsMatch(f.primary_version, f.config_version);
  const showRuntimeBody = Boolean(carId && !loading && !error && summary);

  return (
    <div className="card campaign-runtime-card">
      <div className="card-header campaign-runtime-head">
        <div>
          <h3 className="campaign-runtime-title">
            Vehicle Runtime —{' '}
            <span className="campaign-runtime-car-picker" ref={pickerRef}>
              <button
                type="button"
                className={`campaign-runtime-car-select-btn${pickerOpen ? ' campaign-runtime-car-select-btn-open' : ''}${carId ? ' campaign-runtime-car-select-btn-filled' : ''}`}
                aria-expanded={pickerOpen}
                onClick={() => setPickerOpen((v) => !v)}
              >
                {carId || 'SELECT CAR'}
                <span
                  className="campaign-runtime-car-select-caret"
                  aria-hidden
                >
                  ▾
                </span>
              </button>
              {pickerOpen ? (
                <ul
                  className="campaign-runtime-car-select-menu"
                  role="listbox"
                >
                  <li>
                    <button
                      type="button"
                      role="option"
                      aria-selected={!carId}
                      className={`campaign-runtime-car-select-item campaign-runtime-car-select-item-reset${!carId ? ' campaign-runtime-car-select-item-active' : ''}`}
                      onClick={() => {
                        onSelectCarId('');
                        setPickerOpen(false);
                      }}
                    >
                      SELECT CAR
                    </button>
                  </li>
                  {carOptions.map((id) => (
                    <li key={id}>
                      <button
                        type="button"
                        role="option"
                        aria-selected={carId === id}
                        className={`campaign-runtime-car-select-item campaign-runtime-car-select-item-car${carId === id ? ' campaign-runtime-car-select-item-active' : ''}`}
                        onClick={() => {
                          onSelectCarId(id);
                          setPickerOpen(false);
                        }}
                      >
                        {id}
                      </button>
                    </li>
                  ))}
                </ul>
              ) : null}
            </span>
          </h3>
          <p className="tiny subtle">
            {carId
              ? 'Primary / candidate lanes from deploy state, vehicle runtime, and config.'
              : 'Choose a vehicle to view primary / candidate runtime.'}
          </p>
        </div>
        {showRuntimeBody && dockerOk ? (
          <StatusBadge text="docker OK" className="status-green" />
        ) : null}
        {showRuntimeBody && f.primary_version ? (
          <StatusBadge
            text={`primary ${f.primary_version}`}
            className="status-green"
          />
        ) : null}
        {showRuntimeBody && configSynced ? (
          <StatusBadge text="Config synced" className="status-green" />
        ) : null}
        {showRuntimeBody && !candidateEnabled ? (
          <StatusBadge text="Candidate inactive" className="status-gray" />
        ) : null}
        {showRuntimeBody && candidateEnabled ? (
          <StatusBadge text="Candidate active" className="status-blue" />
        ) : null}
        {showRuntimeBody && f.last_action ? (
          <StatusBadge
            text={`Last action ${f.last_action}`}
            className="status-blue"
          />
        ) : null}
        {preconditionWarnings?.length ? (
          <StatusBadge text="Precondition warning" className="status-yellow" />
        ) : null}
      </div>

      {!carId ? null : loading ? (
        <p className="tiny subtle">Loading runtime summary…</p>
      ) : error ? (
        <p className="text-error tiny">{error}</p>
      ) : null}

      {showRuntimeBody && preconditionWarnings?.length ? (
        <ul className="warning-list tiny" style={{ marginTop: 10 }}>
          {preconditionWarnings.map((w) => (
            <li key={w}>{w}</li>
          ))}
        </ul>
      ) : null}

      {showRuntimeBody ? (
        <>
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
        </>
      ) : null}
    </div>
  );
}

function RunDetailDrawer({ run, loading, onClose }) {
  if (loading) {
    return (
      <div className="campaign-run-detail">
        <div className="campaign-run-detail-head">
          <p className="tiny subtle" style={{ margin: 0 }}>
            Loading run detail…
          </p>
          {onClose ? (
            <button
              type="button"
              className="btn btn-secondary campaign-run-detail-close"
              onClick={onClose}
            >
              Close
            </button>
          ) : null}
        </div>
      </div>
    );
  }
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
        {onClose ? (
          <button
            type="button"
            className="btn btn-secondary campaign-run-detail-close"
            onClick={onClose}
            aria-label="Close run detail"
          >
            Close
          </button>
        ) : null}
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
      <RawJsonDetails label="Raw run JSON" data={run} />
    </div>
  );
}

export default function CampaignManagementPanel({ onVersionMapChanged }) {
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
  const [runsPage, setRunsPage] = useState(1);

  const [runtimeViewCarId, setRuntimeViewCarId] = useState('');
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
  const [stalePendingWarnings, setStalePendingWarnings] = useState([]);
  const [candidateStaged, setCandidateStaged] = useState(null);
  const [candidateStagedLoading, setCandidateStagedLoading] = useState(false);
  const [dropCandidateLoading, setDropCandidateLoading] = useState(false);
  const [dropCandidateError, setDropCandidateError] = useState('');
  const [logsRefreshToken, setLogsRefreshToken] = useState(0);
  const [preferredLogsTab, setPreferredLogsTab] = useState('');
  const [deploymentCarIds, setDeploymentCarIds] = useState([]);
  const [deploymentRegions, setDeploymentRegions] = useState([]);
  const [deploymentModels, setDeploymentModels] = useState([]);
  const [deploymentGroup, setDeploymentGroup] = useState('');
  const [percentRows, setPercentRows] = useState(() => defaultPercentRows());
  const [percentShuffleSeed, setPercentShuffleSeed] = useState(() => Date.now());
  const [percentValidateCache, setPercentValidateCache] = useState(null);
  const [percentCandidateComplete, setPercentCandidateComplete] = useState(false);
  const [percentRunCandidateResult, setPercentRunCandidateResult] = useState(null);
  const [percentPromoteResult, setPercentPromoteResult] = useState(null);
  const [fleetVehicles, setFleetVehicles] = useState([]);
  const [versionMapSyncLoading, setVersionMapSyncLoading] = useState(false);
  const [versionMapSyncNotice, setVersionMapSyncNotice] = useState('');
  const [versionMapSyncError, setVersionMapSyncError] = useState('');

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
      fetchCampaignRuns({ campaign_id: campaignId, limit: 100 }),
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
    if (!selectedCampaignId) return;
    loadDetail(selectedCampaignId);
    setSelectedRunId('');
    setSelectedRun(null);
    setRunsPage(1);
  }, [selectedCampaignId, loadDetail]);

  const percentMode = !selectedCampaignId;

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

  const rolloutGroupProgress = useMemo(
    () =>
      computeRolloutGroupProgress(
        runs,
        fleetVehicles,
        selectedCampaignId,
        campaign?.target_version,
      ),
    [runs, fleetVehicles, selectedCampaignId, campaign?.target_version],
  );

  const resolvedCarIds = useMemo(
    () =>
      resolveDeploymentCarIds({
        vehicles: fleetVehicles,
        carIds: deploymentCarIds,
        regions: deploymentRegions,
        models: deploymentModels,
        group: deploymentGroup,
      }),
    [
      fleetVehicles,
      deploymentCarIds,
      deploymentRegions,
      deploymentModels,
      deploymentGroup,
    ],
  );

  const demoFlowCarId = useMemo(() => {
    if (
      runtimeViewCarId &&
      resolvedCarIds.includes(runtimeViewCarId)
    ) {
      return runtimeViewCarId;
    }
    if (resolvedCarIds.length) return resolvedCarIds[0];
    return runtimeViewCarId || '';
  }, [runtimeViewCarId, resolvedCarIds]);

  const demoFlowFeature = listCampaign?.feature || campaign?.feature || '';
  const demoFlowVersion =
    listCampaign?.target_version || campaign?.target_version || '';

  const deploymentTargetBody = useMemo(
    () =>
      buildRuntimeTargetBody({
        vehicles: fleetVehicles,
        carIds: deploymentCarIds,
        regions: deploymentRegions,
        models: deploymentModels,
        group: deploymentGroup,
      }),
    [
      fleetVehicles,
      deploymentCarIds,
      deploymentRegions,
      deploymentModels,
      deploymentGroup,
    ],
  );

  const deploymentFilterActive = Boolean(
    deploymentRegions.length || deploymentModels.length,
  );

  const rolloutGroupFilterActive = Boolean(deploymentGroup);

  const currentDeployTargetKey = useMemo(
    () => deploymentTargetKey(deploymentTargetBody),
    [deploymentTargetBody],
  );

  const deploymentHasTarget = resolvedCarIds.length > 0;

  const noCampaignSelected = !selectedCampaignId;

  const effectiveResolvedCarIds = useMemo(
    () => (percentMode ? [...ALL_CAR_IDS] : resolvedCarIds),
    [percentMode, resolvedCarIds],
  );

  const effectiveDeploymentHasTarget = percentMode
    ? true
    : deploymentHasTarget;

  const effectiveDeployTargetKey = useMemo(() => {
    if (percentMode) {
      return deploymentTargetKey({ vehicle_ids: ALL_CAR_IDS });
    }
    return currentDeployTargetKey;
  }, [percentMode, currentDeployTargetKey]);

  const deployToLabel = formatDeployTargetLabel({
    carIds: deploymentCarIds,
    regions: deploymentRegions,
    models: deploymentModels,
    group: deploymentGroup,
    vehicles: fleetVehicles,
  });

  const percentConfigValid = useMemo(
    () => isPercentConfigValid(percentRows, campaigns),
    [percentRows, campaigns],
  );

  const percentEnrichedRows = useMemo(
    () => enrichPercentRows(percentRows, campaigns),
    [percentRows, campaigns],
  );

  const percentConfigFingerprint = useMemo(
    () =>
      buildPercentDeployKey(effectiveDeployTargetKey, percentEnrichedRows, {
        random: false,
        seed: 0,
      }),
    [effectiveDeployTargetKey, percentEnrichedRows],
  );

  const prevPercentConfigFingerprint = useRef(percentConfigFingerprint);
  useEffect(() => {
    if (prevPercentConfigFingerprint.current === percentConfigFingerprint) {
      return;
    }
    prevPercentConfigFingerprint.current = percentConfigFingerprint;
    setPercentShuffleSeed(Date.now());
  }, [percentConfigFingerprint]);

  const percentSegments = useMemo(() => {
    if (!percentMode || !percentConfigValid) return [];
    return allocateCarsByPercent(effectiveResolvedCarIds, percentEnrichedRows, {
      random: true,
      seed: percentShuffleSeed,
    });
  }, [
    percentMode,
    percentConfigValid,
    percentEnrichedRows,
    effectiveResolvedCarIds,
    percentShuffleSeed,
  ]);

  const percentDeployKey = useMemo(
    () =>
      buildPercentDeployKey(effectiveDeployTargetKey, percentEnrichedRows, {
        random: true,
        seed: percentShuffleSeed,
      }),
    [effectiveDeployTargetKey, percentEnrichedRows, percentShuffleSeed],
  );

  const percentActionsEnabled = percentMode && percentConfigValid;

  const percentValidateResult = useMemo(() => {
    if (!percentValidateCache) return null;
    if (percentValidateCache._deployTargetKey !== percentDeployKey) return null;
    return percentValidateCache;
  }, [percentValidateCache, percentDeployKey]);

  const loadPercentRuns = useCallback(async () => {
    setRunsLoading(true);
    setRunsError('');
    const campaignIds = [
      ...new Set(
        enrichPercentRows(percentRows, campaigns)
          .map((r) => r.campaignId)
          .filter(Boolean),
      ),
    ];
    try {
      let merged = [];
      if (campaignIds.length) {
        const batches = await Promise.all(
          campaignIds.map((id) =>
            fetchCampaignRuns({ campaign_id: id, limit: 50 }),
          ),
        );
        for (const res of batches) {
          if (res.ok) merged.push(...(res.runs || []));
        }
      } else {
        const res = await fetchCampaignRuns({ limit: 100 });
        if (res.ok) merged = res.runs || [];
      }
      merged.sort((a, b) =>
        String(b.started_at || '').localeCompare(String(a.started_at || '')),
      );
      setRuns(merged);
    } catch (err) {
      setRunsError(err.message || 'Failed to load runs');
      setRuns([]);
    }
    setRunsLoading(false);
  }, [percentRows, campaigns]);

  useEffect(() => {
    if (!percentMode) return;
    setRunsPage(1);
    loadPercentRuns();
  }, [percentMode, percentDeployKey, loadPercentRuns]);

  useEffect(() => {
    const totalPages = Math.max(
      1,
      Math.ceil((runs?.length || 0) / RUNS_PAGE_SIZE),
    );
    if (runsPage > totalPages) setRunsPage(totalPages);
  }, [runs.length, runsPage]);

  const closeRunDetail = useCallback(() => {
    setSelectedRunId('');
    setSelectedRun(null);
  }, []);

  const runtimeCarId = runtimeViewCarId;

  const displayFeature = useMemo(() => {
    if (percentMode) {
      return (
        percentEnrichedRows.find((r) => r.feature)?.feature || 'Navigation'
      );
    }
    return feature;
  }, [percentMode, percentEnrichedRows, feature]);

  useEffect(() => {
    loadRuntime(runtimeViewCarId);
  }, [runtimeViewCarId, loadRuntime]);

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

  const displayValidateResult = percentMode
    ? percentValidateResult
    : validateResult;

  const displayRunCandidateResult = percentMode
    ? percentCandidateAsRunResult(percentRunCandidateResult)
    : runCandidateResult;

  const displayPromoteResult = percentMode
    ? percentPromoteAsRunResult(percentPromoteResult)
    : promoteResult;

  const displayPromoteForBanner = percentMode
    ? percentPromoteResult?.ok
      ? { ok: true, phase: 'promote' }
      : null
    : promoteResult;

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
    if (percentMode || !selectedCampaignId || !deploymentHasTarget) {
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
    percentMode,
    selectedCampaignId,
    deploymentHasTarget,
    deploymentTargetBody,
    currentDeployTargetKey,
  ]);

  useEffect(() => {
    refreshCandidateStaged();
  }, [refreshCandidateStaged]);

  useEffect(() => {
    if (percentMode || !deploymentHasTarget || !resolvedCarIds.length || !feature) {
      setStalePendingWarnings([]);
      return undefined;
    }
    let cancelled = false;
    fetchStalePendingWarnings(resolvedCarIds, feature).then((res) => {
      if (!cancelled) {
        setStalePendingWarnings(Array.isArray(res.warnings) ? res.warnings : []);
      }
    });
    return () => {
      cancelled = true;
    };
  }, [
    percentMode,
    deploymentHasTarget,
    resolvedCarIds,
    feature,
    currentDeployTargetKey,
  ]);

  useEffect(() => {
    if (!percentMode) return;
    setPercentValidateCache((cur) => {
      if (!cur || cur._deployTargetKey === percentDeployKey) return cur;
      return null;
    });
    setPercentCandidateComplete(false);
    setPercentRunCandidateResult(null);
    setPercentPromoteResult(null);
  }, [percentMode, percentDeployKey]);

  const canRunCandidatePercent = useMemo(() => {
    if (!percentActionsEnabled || !percentValidateResult) return false;
    if (runCandidateLoading || promoteLoading) return false;
    return (percentValidateResult.summary?.fail ?? 0) === 0;
  }, [
    percentActionsEnabled,
    percentValidateResult,
    runCandidateLoading,
    promoteLoading,
  ]);

  const canPromotePercent = useMemo(() => {
    if (!percentActionsEnabled || !percentValidateResult) return false;
    if ((percentValidateResult.summary?.fail ?? 0) > 0) return false;
    if (!percentCandidateComplete) return false;
    if (runCandidateLoading || promoteLoading) return false;
    return true;
  }, [
    percentActionsEnabled,
    percentValidateResult,
    percentCandidateComplete,
    runCandidateLoading,
    promoteLoading,
  ]);

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

  const activeValidateResult = percentMode
    ? percentValidateResult
    : validateResult;
  const activeImageAvailable = percentMode ? true : imageAvailable;
  const activeCanRunCandidate = percentMode
    ? canRunCandidatePercent
    : canRunCandidate;
  const activeCanPromote = percentMode ? canPromotePercent : canPromote;
  const activeActionsEnabled = percentMode
    ? percentActionsEnabled
    : Boolean(selectedCampaignId);

  const updateModeWarnings = useMemo(() => {
    const items = activeValidateResult?.items || [];
    return items
      .filter(
        (i) =>
          i.level === 'WARN' &&
          /update_mode|not READY/i.test(String(i.message || '')),
      )
      .map((i) => i.message);
  }, [activeValidateResult]);

  const preconditionWarnings = useMemo(() => {
    const items = activeValidateResult?.items || [];
    return items
      .filter(
        (i) =>
          i.level === 'WARN' &&
          /update_mode|READY|OTA|approval/i.test(String(i.message || '')),
      )
      .map((i) => i.message);
  }, [activeValidateResult]);

  const applyDemoReadyBeforeAction = useCallback(
    async ({ carIds, feature: feat, segments }) => {
      if (segments?.length) {
        for (const seg of segments) {
          const res = await prepareAllTargetsDemoReady(seg.carIds, seg.feature);
          if (!res.ok) {
            const detail = (res.failed || [])
              .map((f) => f.car_id)
              .join(', ');
            return {
              ok: false,
              error: `Prepare Demo READY failed (${detail || 'unknown'})`,
            };
          }
        }
        return { ok: true };
      }
      if (carIds?.length && feat) {
        const res = await prepareAllTargetsDemoReady(carIds, feat);
        if (!res.ok) {
          const detail = (res.failed || []).map((f) => f.car_id).join(', ');
          return {
            ok: false,
            error: `Prepare Demo READY failed (${detail || 'unknown'})`,
          };
        }
      }
      return { ok: true };
    },
    [],
  );

  const afterDemoReadyRefresh = useCallback(async () => {
    if (runtimeCarId) {
      await loadRuntime(runtimeCarId);
    }
    await refreshCandidateStaged();
  }, [runtimeCarId, loadRuntime, refreshCandidateStaged]);

  const runValidate = useCallback(async () => {
    if (!selectedCampaignId || !deploymentHasTarget) return;
    setValidateLoading(true);
    setValidateError('');
    const prep = await applyDemoReadyBeforeAction({
      carIds: resolvedCarIds,
      feature,
    });
    if (!prep.ok) {
      setValidateError(prep.error);
      setValidateLoading(false);
      return;
    }
    await afterDemoReadyRefresh();
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
    resolvedCarIds,
    feature,
    applyDemoReadyBeforeAction,
    afterDemoReadyRefresh,
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
    const prep = await applyDemoReadyBeforeAction({
      carIds: resolvedCarIds,
      feature,
    });
    if (!prep.ok) {
      setRunCandidateError(prep.error);
      setRunCandidateLoading(false);
      return;
    }
    await afterDemoReadyRefresh();
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
    resolvedCarIds,
    feature,
    applyDemoReadyBeforeAction,
    afterDemoReadyRefresh,
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

    const prep = await applyDemoReadyBeforeAction({
      carIds: resolvedCarIds,
      feature,
    });
    if (!prep.ok) {
      setPromoteError(prep.error);
      return;
    }
    await afterDemoReadyRefresh();

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
        const applyRes = await applyPendingNativeForCars(resolvedCarIds, {
          feature,
        });
        if (applyRes.ok === false) {
          const failedCars = (applyRes.results || [])
            .filter((row) => row.ok === false)
            .map((row) => row.car_id)
            .join(', ');
          setPromoteError(
            applyRes.error ||
              `apply-pending-native failed on one or more vehicles${failedCars ? ` (${failedCars})` : ''}`,
          );
        }
        const warnRes = await fetchStalePendingWarnings(resolvedCarIds, feature);
        setStalePendingWarnings(warnRes.warnings || []);
      }

      if (res.ok && resolvedCarIds.length && campaign?.target_version) {
        setPromotePhase('verify');
        const synced = await waitForPromoteOnTargets(
          resolvedCarIds,
          feature,
          campaign.target_version,
        );
        if (synced) {
          let msg =
            resolvedCarIds.length > 1
              ? `Promote complete — primary ${campaign.target_version} on ${resolvedCarIds.length} vehicles.`
              : `Promote complete — primary ${campaign.target_version} on ${resolvedCarIds[0]}.`;
          const nextGroup = deploymentGroup
            ? nextRolloutGroupAfter(deploymentGroup)
            : null;
          if (deploymentGroup && nextGroup) {
            msg += ` Next: select ${nextGroup} and repeat validate → candidate → promote.`;
          } else if (
            deploymentGroup ===
            FLEET_ROLLOUT_GROUPS[FLEET_ROLLOUT_GROUPS.length - 1]
          ) {
            msg += ' Full fleet rollout complete for this version.';
          }
          setPromoteCompleteMessage(msg);
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
    deploymentGroup,
    applyDemoReadyBeforeAction,
    afterDemoReadyRefresh,
  ]);

  const runPercentValidate = useCallback(async () => {
    if (!percentActionsEnabled || !percentSegments.length) return;
    setValidateLoading(true);
    setValidateError('');
    const prep = await applyDemoReadyBeforeAction({
      segments: percentSegments,
    });
    if (!prep.ok) {
      setValidateError(prep.error);
      setValidateLoading(false);
      return;
    }
    await afterDemoReadyRefresh();
    const segmentResults = [];
    for (const seg of percentSegments) {
      const res = await validateCampaign(seg.campaignId, {
        vehicle_ids: seg.carIds,
      });
      segmentResults.push({ ...seg, res });
    }
    const aggregated = aggregateValidateResults(segmentResults);
    setPercentValidateCache({
      ...aggregated,
      _deployTargetKey: percentDeployKey,
    });
    setPercentCandidateComplete(false);
    if ((aggregated.summary?.fail ?? 0) > 0) {
      setValidateError('One or more proportion segments failed validation.');
    } else {
      setValidateError('');
    }
    setValidateLoading(false);
  }, [
    percentActionsEnabled,
    percentSegments,
    percentDeployKey,
    applyDemoReadyBeforeAction,
    afterDemoReadyRefresh,
  ]);

  const runPercentCandidate = useCallback(async () => {
    if (!canRunCandidatePercent || !percentSegments.length) return;
    setRunCandidateLoading(true);
    setRunCandidateError('');
    setPercentCandidateComplete(false);
    const prep = await applyDemoReadyBeforeAction({
      segments: percentSegments,
    });
    if (!prep.ok) {
      setRunCandidateError(prep.error);
      setRunCandidateLoading(false);
      return;
    }
    await afterDemoReadyRefresh();
    const segmentResults = [];
    let allOk = true;
    for (const seg of percentSegments) {
      const res = await runCampaignPhase(seg.campaignId, 'candidate', {
        vehicle_ids: seg.carIds,
      });
      segmentResults.push({ ...seg, res });
      if (!res.ok) allOk = false;
    }
    setPercentRunCandidateResult({ ok: allOk, segments: segmentResults });
    setPercentCandidateComplete(allOk);
    if (!allOk) {
      setRunCandidateError(
        'One or more proportion segments failed candidate deploy.',
      );
    } else {
      setRunCandidateError('');
    }
    setRunCandidateLoading(false);
    await loadList();
    await loadPercentRuns();
  }, [
    canRunCandidatePercent,
    percentSegments,
    loadList,
    loadPercentRuns,
    applyDemoReadyBeforeAction,
    afterDemoReadyRefresh,
  ]);

  const runPercentDropCandidate = useCallback(async () => {
    if (!percentActionsEnabled || !percentSegments.length) return;
    setDropCandidateLoading(true);
    setDropCandidateError('');
    let allOk = true;
    for (const seg of percentSegments) {
      const res = await dropCampaignCandidate(seg.campaignId, {
        vehicle_ids: seg.carIds,
      });
      if (!res.ok) allOk = false;
    }
    if (!allOk) {
      setDropCandidateError('Drop candidate failed on one or more segments.');
    } else {
      setDropCandidateError('');
      setPercentCandidateComplete(false);
      setPercentRunCandidateResult(null);
    }
    setDropCandidateLoading(false);
  }, [percentActionsEnabled, percentSegments]);

  const runPercentPromote = useCallback(async () => {
    if (!canPromotePercent || !percentSegments.length) return;
    setPromoteLoading(true);
    setPromotePhase('campaign');
    setPromoteError('');
    setPromoteCompleteMessage('');

    try {
      const prep = await applyDemoReadyBeforeAction({
        segments: percentSegments,
      });
      if (!prep.ok) {
        setPromoteError(prep.error);
        return;
      }
      await afterDemoReadyRefresh();

      const segmentResults = [];
      let allOk = true;
      for (const seg of percentSegments) {
        setPromotePhase(`promote ${seg.feature} ${seg.version}`);
        const res = await runCampaignPhase(seg.campaignId, 'promote', {
          vehicle_ids: seg.carIds,
        });
        segmentResults.push({ ...seg, res });
        if (!res.ok) allOk = false;
      }
      setPercentPromoteResult({ ok: allOk, segments: segmentResults });

      if (allOk) {
        const allCars = sortCarIds(
          percentSegments.flatMap((s) => s.carIds || []),
        );
        setPromotePhase('docker');
        try {
          await postVehicleRuntimeApplyPending();
        } catch {
          /* optional */
        }
        setPromotePhase('apply-native');
        for (const seg of percentSegments) {
          const applyRes = await applyPendingNativeForCars(seg.carIds, {
            feature: seg.feature,
          });
          if (applyRes.ok === false) {
            allOk = false;
            const failedCars = (applyRes.results || [])
              .filter((row) => row.ok === false)
              .map((row) => row.car_id)
              .join(', ');
            setPromoteError(
              applyRes.error ||
                `apply-pending-native failed for ${seg.feature}${failedCars ? ` (${failedCars})` : ''}`,
            );
          }
        }
        setPromotePhase('verify');
        let synced = true;
        for (const seg of percentSegments) {
          const ok = await waitForPromoteOnTargets(
            seg.carIds,
            seg.feature,
            seg.version,
          );
          if (!ok) synced = false;
        }
        setPromoteCompleteMessage(
          synced
            ? 'Percent deploy complete — all segments promoted on target vehicles.'
            : 'Promote commands finished; refresh vehicle_runtime if JSON lags.',
        );
      } else {
        setPromoteError('Promote failed on one or more segments.');
      }
      await loadList();
      await loadPercentRuns();
    } finally {
      setPromoteLoading(false);
      setPromotePhase('');
    }
  }, [
    canPromotePercent,
    percentSegments,
    loadList,
    loadPercentRuns,
    waitForPromoteOnTargets,
    applyDemoReadyBeforeAction,
    afterDemoReadyRefresh,
  ]);

  useEffect(() => {
    if (!deploymentGroup) return;
    if (rolloutGroupProgress[deploymentGroup]?.unlocked) return;
    setDeploymentGroup('');
  }, [deploymentGroup, rolloutGroupProgress]);

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

  useEffect(() => {
    if (!selectedCampaignId) {
      setVersionMapSyncNotice('');
      setVersionMapSyncError('');
      setVersionMapSyncLoading(false);
      return undefined;
    }

    let cancelled = false;
    setVersionMapSyncLoading(true);
    setVersionMapSyncNotice('');
    setVersionMapSyncError('');

    (async () => {
      const res = await syncCampaignVersionMap(selectedCampaignId);
      if (cancelled) return;
      setVersionMapSyncLoading(false);
      if (!res.ok) {
        setVersionMapSyncError(res.error || 'version_map sync failed');
        return;
      }
      if (res.unchanged) {
        setVersionMapSyncNotice('');
        return;
      }
      setVersionMapSyncNotice(
        res.message ||
          `${res.feature} slot ${res.slot} → ${res.assigned?.version || ''}`,
      );
      setValidateCache((prev) => {
        if (!prev[selectedCampaignId]) return prev;
        const next = { ...prev };
        delete next[selectedCampaignId];
        return next;
      });
      await onVersionMapChanged?.();
    })();

    return () => {
      cancelled = true;
    };
  }, [selectedCampaignId, onVersionMapChanged]);

  const onSelectCampaign = (id) => {
    setSelectedCampaignId((cur) => {
      const next = cur === id ? '' : id;
      if (next !== cur) {
        setDeploymentCarIds([]);
        setDeploymentRegions([]);
        setDeploymentModels([]);
        setDeploymentGroup('');
        setValidateError('');
        setRunCandidateError('');
        setPromoteError('');
        setPromoteCompleteMessage('');
        setPercentValidateCache(null);
        setPercentCandidateComplete(false);
        setPercentRunCandidateResult(null);
        setPercentPromoteResult(null);
        if (next) {
          setDetail(null);
          setRuns([]);
        }
      }
      return next;
    });
  };

  const onToggleCar = (carId) => {
    setDeploymentGroup('');
    setDeploymentRegions([]);
    setDeploymentModels([]);
    setDeploymentCarIds((prev) => {
      const set = new Set(prev);
      if (set.has(carId)) set.delete(carId);
      else set.add(carId);
      return sortCarIds([...set]);
    });
  };

  const onToggleAllCars = () => {
    setDeploymentGroup('');
    setDeploymentRegions([]);
    setDeploymentModels([]);
    setDeploymentCarIds((prev) =>
      allCarsSelected(prev) ? [] : [...ALL_CAR_IDS],
    );
  };

  const onToggleRegion = (region) => {
    setDeploymentGroup('');
    setDeploymentCarIds([]);
    setDeploymentRegions((prev) => {
      const set = new Set(prev);
      if (set.has(region)) set.delete(region);
      else set.add(region);
      return FLEET_REGIONS.filter((r) => set.has(r));
    });
  };

  const onToggleModel = (model) => {
    setDeploymentGroup('');
    setDeploymentCarIds([]);
    setDeploymentModels((prev) => {
      const set = new Set(prev);
      if (set.has(model)) set.delete(model);
      else set.add(model);
      return FLEET_MODELS.filter((m) => set.has(m));
    });
  };

  const onSelectRolloutGroup = (group) => {
    if (!rolloutGroupProgress[group]?.unlocked) return;
    setDeploymentCarIds([]);
    setDeploymentRegions([]);
    setDeploymentModels([]);
    setDeploymentGroup((cur) => (cur === group ? '' : group));
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
    else if (percentMode) loadPercentRuns();
  };

  const policyFlow = campaign?.rollout_policy_id
    ? policyFlowById.get(campaign.rollout_policy_id)
    : '';

  const promoteDemoComplete = useMemo(() => {
    if (percentMode) {
      return Boolean(percentPromoteResult?.ok);
    }
    const fromRuns = (runs || []).some(
      (r) =>
        String(r.phase || '').toLowerCase() === 'promote' && isRunSuccess(r),
    );
    return (
      fromRuns ||
      (promoteResult?.ok &&
        String(promoteResult.phase || '').toLowerCase() === 'promote')
    );
  }, [percentMode, percentPromoteResult, runs, promoteResult]);

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

        <CampaignDemoFlow
          feature={demoFlowFeature}
          version={demoFlowVersion}
          carId={demoFlowCarId}
          carIds={resolvedCarIds}
          group={deploymentGroup}
          vehicles={fleetVehicles}
          percentMode={percentMode}
        />

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

      <div className="campaign-detail-flow">
        {versionMapSyncLoading ? (
          <p className="tiny subtle campaign-vm-sync-hint">Syncing version_map…</p>
        ) : null}
        {!versionMapSyncLoading && versionMapSyncNotice ? (
          <p className="tiny campaign-vm-sync-ok">
            version_map updated: {versionMapSyncNotice}
          </p>
        ) : null}
        {!versionMapSyncLoading && versionMapSyncError ? (
          <p className="tiny campaign-vm-sync-err">{versionMapSyncError}</p>
        ) : null}
        {selectedCampaignId ? (
          <CampaignSelectedHeader
            campaign={campaign}
            policy={policy}
            policyFlow={policyFlow}
            deployToLabel={deployToLabel}
            imageAvailable={listCampaign?.image_available}
          />
        ) : (
          <div className="card campaign-detail-card campaign-percent-intro">
            <p className="tiny subtle">
              <strong>Percent deploy:</strong> leave all campaign rows
              unselected. Splits the full fleet (car_1–car_10) by proportion
              below (must total 100%), then Validate → Run Candidate → Promote
              Primary per segment.
            </p>
          </div>
        )}

        <CampaignTargetSelector
          selectedCarIds={deploymentCarIds}
          onToggleCar={onToggleCar}
          onToggleAll={onToggleAllCars}
          disabled={noCampaignSelected}
          noCampaignSelected={noCampaignSelected}
          filterActive={deploymentFilterActive || rolloutGroupFilterActive}
        />

        <div className="campaign-region-model-row">
          <CampaignGroupSelector
            selectedRegions={deploymentRegions}
            onToggleRegion={onToggleRegion}
            selectedModels={deploymentModels}
            vehicles={fleetVehicles}
            disabled={noCampaignSelected}
            noCampaignSelected={noCampaignSelected}
            vehiclesDisabled={
              deploymentCarIds.length > 0 || rolloutGroupFilterActive
            }
          />

          <CampaignModelSelector
            selectedModels={deploymentModels}
            onToggleModel={onToggleModel}
            selectedRegions={deploymentRegions}
            vehicles={fleetVehicles}
            disabled={noCampaignSelected}
            noCampaignSelected={noCampaignSelected}
            vehiclesDisabled={
              deploymentCarIds.length > 0 || rolloutGroupFilterActive
            }
          />
        </div>

        <CampaignRolloutGroupSelector
          selectedGroup={deploymentGroup}
          onSelectGroup={onSelectRolloutGroup}
          rolloutProgress={rolloutGroupProgress}
          vehicles={fleetVehicles}
          disabled={noCampaignSelected}
          noCampaignSelected={noCampaignSelected}
          filterDisabled={deploymentCarIds.length > 0 || deploymentFilterActive}
        />

        <CampaignPercentDeployCard
          rows={percentRows}
          onChangeRows={setPercentRows}
          campaigns={campaigns}
          resolvedCarIds={effectiveResolvedCarIds}
          disabled={Boolean(selectedCampaignId)}
          shuffleSeed={percentShuffleSeed}
          onReshuffle={() => setPercentShuffleSeed(Date.now())}
        />

        {selectedCampaignId && deploymentHasTarget && resolvedCarIds.length > 1 ? (
          <p className="tiny subtle campaign-multi-target-hint">
            Multi-target deploy: Validate checks all selected vehicles (
            {resolvedCarIds.length}).
          </p>
        ) : null}

        <CampaignActionBar
          actionsEnabled={activeActionsEnabled}
          selectedCampaignId={selectedCampaignId || '__percent__'}
          onValidate={percentMode ? runPercentValidate : runValidate}
          validateLoading={validateLoading}
          onRunCandidate={percentMode ? runPercentCandidate : runCandidate}
          runCandidateLoading={runCandidateLoading}
          canRunCandidate={activeCanRunCandidate}
          onPromote={percentMode ? runPercentPromote : runPromote}
          promoteLoading={promoteLoading}
          promotePhase={promotePhase}
          promoteCompleteMessage={promoteCompleteMessage}
          canPromote={activeCanPromote}
          validateResult={activeValidateResult}
          updateModeWarnings={updateModeWarnings}
          hasFailedCandidateRun={percentMode ? false : hasFailedCandidateRun}
          deploymentHasTarget={
            percentMode ? effectiveDeploymentHasTarget : deploymentHasTarget
          }
          targetVehicleCount={
            percentMode ? effectiveResolvedCarIds.length : resolvedCarIds.length
          }
          imageAvailable={activeImageAvailable}
          candidateStagedInRuntime={
            !percentMode &&
            candidateStagedForTarget &&
            !latestCandidateSuccess
          }
          onDropCandidate={
            percentMode ? runPercentDropCandidate : runDropCandidate
          }
          dropCandidateLoading={dropCandidateLoading}
          canDropCandidate={
            percentMode ? percentActionsEnabled : canDropCandidate
          }
        />

        {dropCandidateError ? (
          <p className="tiny campaign-logs-alert campaign-logs-alert-fail">
            {dropCandidateError}
          </p>
        ) : null}

        {promoteError ? (
          <p className="tiny campaign-logs-alert campaign-logs-alert-fail">
            {promoteError}
          </p>
        ) : null}

        {stalePendingWarnings.length && !promoteLoading ? (
          <p className="tiny campaign-logs-alert campaign-logs-alert-muted">
            Other feature pending on target vehicles (promote applies{' '}
            <strong>{feature || 'campaign feature'}</strong> only):{' '}
            {stalePendingWarnings.map((w) => w.message).join(' · ')}
          </p>
        ) : null}

        {promoteCompleteMessage && !promoteLoading ? (
          <p className="tiny campaign-logs-alert campaign-promote-complete-msg">
            {promoteCompleteMessage}
          </p>
        ) : null}

        <CampaignFinalStateBanner
            runs={runs}
            runtimeSummary={runtimeSummary}
            feature={displayFeature}
            promoteResult={displayPromoteForBanner}
          />

          <section className="campaign-results-stack">
            {promoteDemoComplete ? (
              <details className="campaign-result-collapsed">
                <summary>Validate result</summary>
                <CampaignValidateResultPanel
                  result={displayValidateResult}
                  error={validateError}
                  loading={validateLoading}
                />
              </details>
            ) : (
              <CampaignValidateResultPanel
                result={displayValidateResult}
                error={validateError}
                loading={validateLoading}
              />
            )}

            {(displayRunCandidateResult || runCandidateLoading) && (
              <CampaignExecutionResultPanel
                title={
                  percentMode
                    ? 'Run Candidate Result (percent)'
                    : 'Run Candidate Result'
                }
                subtitle="campaign-run --phase candidate"
                mode="run"
                runPhase="candidate"
                loadingLabel="Running candidate phase…"
                result={displayRunCandidateResult}
                error={runCandidateError}
                loading={runCandidateLoading}
              />
            )}

            {(displayPromoteResult || promoteLoading) && (
              <CampaignExecutionResultPanel
                title={
                  percentMode ? 'Promote Result (percent)' : 'Promote Primary Result'
                }
                subtitle="campaign-run --phase promote"
                mode="run"
                runPhase="promote"
                loadingLabel="Promoting candidate to primary…"
                result={displayPromoteResult}
                error={promoteError}
                loading={promoteLoading}
              />
            )}
          </section>

          <RuntimeSummaryCard
            carId={runtimeViewCarId}
            carOptions={ALL_CAR_IDS}
            onSelectCarId={setRuntimeViewCarId}
            feature={displayFeature}
            summary={runtimeSummary}
            loading={runtimeLoading}
            error={runtimeError}
            preconditionWarnings={preconditionWarnings}
          />

          <CampaignLogsPanel
            carId={runtimeViewCarId}
            feature={displayFeature}
            refreshToken={logsRefreshToken}
            preferredTab={preferredLogsTab}
            onPreferredTabApplied={() => setPreferredLogsTab('')}
          />

          <CampaignRunHistoryTable
            runs={runs}
            runsLoading={runsLoading}
            runsError={runsError}
            selectedRunId={selectedRunId}
            onSelectRunId={setSelectedRunId}
            onRefresh={refreshRuns}
            selectedRun={selectedRun}
            runDetailLoading={runDetailLoading}
            onCloseRunDetail={closeRunDetail}
            runsPage={runsPage}
            onRunsPageChange={setRunsPage}
          />

        {selectedCampaignId ? (
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
        ) : null}
      </div>
    </div>
  );
}
