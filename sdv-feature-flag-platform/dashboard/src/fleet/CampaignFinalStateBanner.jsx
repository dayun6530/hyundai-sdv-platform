import { StatusBadge } from './campaignShared.jsx';
import {
  findLatestSuccessfulCandidateRun,
  isRunSuccess,
  versionsMatch,
} from './campaignUiModel.js';

function findLatestSuccessfulPromoteRun(runs) {
  for (const run of runs || []) {
    if (
      String(run?.phase || '').toLowerCase() === 'promote' &&
      isRunSuccess(run)
    ) {
      return run;
    }
  }
  return null;
}

export default function CampaignFinalStateBanner({
  runs,
  runtimeSummary,
  feature,
  promoteResult,
}) {
  const promoteRun = findLatestSuccessfulPromoteRun(runs);
  const promoteOk =
    Boolean(promoteRun) ||
    (promoteResult?.ok &&
      String(promoteResult.phase || '').toLowerCase() === 'promote');
  const candidateRun = findLatestSuccessfulCandidateRun(runs);

  if (!promoteOk) return null;

  const f =
    runtimeSummary?.features?.[feature] || runtimeSummary?.features?.ADAS || {};
  const configSynced = versionsMatch(f.primary_version, f.config_version);
  const candidateInactive = !f.shadow_enabled;
  const dockerOk =
    String(f.docker_runtime_result || '').toLowerCase() === 'success';

  return (
    <div className="card campaign-final-state-card">
      <div className="card-header campaign-final-state-head">
        <div>
          <h3>Demo complete — primary promoted</h3>
          <p className="tiny subtle">
            Final state after successful promote (verify before presenting).
          </p>
        </div>
      </div>
      <div className="vd-badge-row campaign-final-badges">
        {f.primary_version ? (
          <StatusBadge
            text={`Primary ${f.primary_version}`}
            className="status-green"
          />
        ) : null}
        {candidateInactive ? (
          <StatusBadge text="Candidate inactive" className="status-gray" />
        ) : null}
        {configSynced ? (
          <StatusBadge text="Config synced" className="status-green" />
        ) : null}
        {dockerOk ? (
          <StatusBadge text="Docker runtime success" className="status-green" />
        ) : null}
        {String(f.last_action || '')
          .toLowerCase()
          .includes('promote') ? (
          <StatusBadge
            text={`Last action ${f.last_action}`}
            className="status-blue"
          />
        ) : f.last_action ? (
          <StatusBadge
            text={`Last action ${f.last_action}`}
            className="status-blue"
          />
        ) : null}
        {promoteRun ? (
          <StatusBadge text="Latest promote success" className="status-green" />
        ) : null}
        {candidateRun ? (
          <StatusBadge text="Candidate run success" className="status-green" />
        ) : null}
      </div>
    </div>
  );
}
