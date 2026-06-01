import {
  DEMO_OTA_FAILURE_HINT,
  RawJsonDetails,
  StatusBadge,
} from './campaignShared.jsx';

function statusBadgeClass(status) {
  const s = String(status || '').toUpperCase();
  if (s === 'FAIL' || s === 'FAILED') return 'status-red';
  if (s === 'PARTIAL') return 'status-yellow';
  if (s.includes('WARN')) return 'status-yellow';
  return 'status-green';
}

function itemLevelClass(level) {
  const l = String(level || '').toUpperCase();
  if (l === 'FAIL') return 'campaign-validate-item-fail';
  if (l === 'WARN') return 'campaign-validate-item-warn';
  return 'campaign-validate-item-ok';
}

function isUpdateModeMessage(msg) {
  return /update_mode|not READY|OTA/i.test(String(msg || ''));
}

export default function CampaignExecutionResultPanel({
  title,
  subtitle,
  result,
  error,
  loading,
  loadingLabel,
  mode = 'validate',
  runPhase,
}) {
  if (loading) {
    return (
      <div className="card campaign-validate-card">
        <h3>{title}</h3>
        <p className="tiny subtle">{loadingLabel}</p>
      </div>
    );
  }

  if (error && !result) {
    return (
      <div className="card campaign-validate-card">
        <h3>{title}</h3>
        <div className="campaign-logs-alert campaign-logs-alert-error">
          {error}
        </div>
      </div>
    );
  }

  if (!result) return null;

  const isRun = mode === 'run';
  const phase = String(runPhase || result.phase || 'candidate').toLowerCase();
  const isPromote = phase === 'promote';
  const summary =
    result.summary ||
    (isRun ? { total: 0, success: 0, failed: 0 } : { ok: 0, warn: 0, fail: 0 });
  const items = result.items || [];
  const status =
    result.status || (result.ok ? (isRun ? 'SUCCESS' : 'OK') : 'FAIL');
  const failed = isRun ? (summary.failed ?? 0) : (summary.fail ?? 0);
  const updateModeHints = items.filter((i) => isUpdateModeMessage(i.message));
  const dockerOk = items.some((i) =>
    /docker_runtime_result=success|docker runtime (candidate|promote) success/i.test(
      i.message,
    ),
  );

  return (
    <div
      className={`card campaign-validate-card ${failed > 0 ? 'campaign-validate-fail' : ''} ${
        isRun ? 'campaign-run-result-card' : ''
      }`}
    >
      <div className="card-header">
        <div>
          <h3>{title}</h3>
          <p className="tiny subtle">{subtitle}</p>
        </div>
        <StatusBadge text={status} className={statusBadgeClass(status)} />
      </div>

      <div className="campaign-validate-summary">
        {isRun ? (
          <span className="mono">
            total <strong>{summary.total ?? 0}</strong> / success{' '}
            <strong>{summary.success ?? 0}</strong> / failed{' '}
            <strong>{summary.failed ?? 0}</strong>
          </span>
        ) : (
          <span className="mono">
            OK <strong>{summary.ok}</strong> / WARN{' '}
            <strong>{summary.warn}</strong> / FAIL{' '}
            <strong>{summary.fail}</strong>
          </span>
        )}
        {isRun && result.ok && isPromote ? (
          <>
            <StatusBadge
              text="Primary promote success"
              className="status-green"
            />
            {dockerOk ? (
              <StatusBadge
                text="Primary container updated"
                className="status-green"
              />
            ) : null}
            <StatusBadge
              text="Primary logs available"
              className="status-green"
            />
          </>
        ) : null}
        {isRun && result.ok && !isPromote ? (
          <>
            <StatusBadge
              text="Candidate run success"
              className="status-green"
            />
            {dockerOk ? (
              <StatusBadge
                text="Docker runtime candidate success"
                className="status-green"
              />
            ) : null}
            <StatusBadge
              text="Candidate logs available"
              className="status-green"
            />
          </>
        ) : null}
        {!isRun && failed === 0 && result.ok ? (
          <StatusBadge
            text="ready for candidate run"
            className="status-green"
          />
        ) : null}
        {!isRun && failed > 0 ? (
          <StatusBadge text="fix failures before run" className="status-red" />
        ) : null}
        {isRun && result.run_record ? (
          <span className="tiny mono subtle" title={result.run_record}>
            {result.run_id || result.run_record}
          </span>
        ) : null}
      </div>

      {error ? (
        <div className="campaign-logs-alert campaign-logs-alert-error tiny">
          {error}
        </div>
      ) : null}

      {isRun && !result.ok && updateModeHints.length ? (
        <div className="campaign-logs-alert campaign-logs-alert-muted tiny">
          {DEMO_OTA_FAILURE_HINT}
        </div>
      ) : null}
      {isRun &&
      !result.ok &&
      !updateModeHints.length &&
      items.some((i) => i.level === 'FAIL') ? (
        <div className="campaign-logs-alert campaign-logs-alert-error tiny">
          {items.find((i) => i.level === 'FAIL')?.message ||
            'Run failed — see items and raw output below.'}
        </div>
      ) : null}

      <ul className="campaign-validate-items">
        {items.map((item, idx) => (
          <li
            key={`${item.level}-${idx}`}
            className={itemLevelClass(item.level)}
          >
            <StatusBadge
              text={item.level}
              className={statusBadgeClass(
                item.level === 'WARN' ? 'OK with WARN' : item.level,
              )}
            />
            <span>{item.message}</span>
          </li>
        ))}
      </ul>

      {items.length === 0 ? (
        <p className="tiny subtle">No parsed output lines.</p>
      ) : null}

      <RawJsonDetails label="Raw stdout" data={result.stdout} />
      {result.stderr ? (
        <RawJsonDetails label="Raw stderr" data={result.stderr} />
      ) : null}
    </div>
  );
}
