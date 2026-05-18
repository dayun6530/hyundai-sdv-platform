function runCandidateButtonTitle({ validateResult, runCandidateLoading, canRunCandidate }) {
  if (runCandidateLoading) return "Running candidate…";
  if (!validateResult) return "Validate first";
  if ((validateResult.summary?.fail ?? 0) > 0) return "Fix validation failures";
  if (canRunCandidate) return "Run Candidate";
  return "Validate first";
}

function promoteButtonTitle({
  validateResult,
  promoteLoading,
  runCandidateLoading,
  canPromote,
  hasFailedCandidateRun,
}) {
  if (promoteLoading) return "Promoting…";
  if (!validateResult) return "Validate first";
  if ((validateResult.summary?.fail ?? 0) > 0) return "Fix validation failures";
  if (runCandidateLoading) return "Run candidate first";
  if (hasFailedCandidateRun && !canPromote) return "Candidate must succeed first";
  if (!canPromote) return "Run candidate first";
  return "Promote Primary";
}

function actionHintValidate({ validateResult, validateLoading }) {
  if (validateLoading) return "Running campaign-validate…";
  if (!validateResult) return "Check campaign readiness before deploy steps.";
  const fail = validateResult.summary?.fail ?? 0;
  if (fail > 0) return "Fix FAIL items before Run Candidate.";
  return "fail=0 — Run Candidate is available.";
}

function actionHintCandidate({ validateResult, runCandidateLoading, canRunCandidate }) {
  if (runCandidateLoading) return "Deploying candidate via campaign-run…";
  if (!validateResult) return "Validate first.";
  if ((validateResult.summary?.fail ?? 0) > 0) return "Fix validation failures first.";
  if (canRunCandidate) return "Deploys candidate container (Docker runtime).";
  return "Validate first.";
}

function actionHintPromote({ canPromote, promoteLoading, runCandidateLoading, hasFailedCandidateRun }) {
  if (promoteLoading) return "Promoting candidate to primary…";
  if (runCandidateLoading) return "Wait for candidate run to finish.";
  if (hasFailedCandidateRun) return "Latest candidate run must succeed.";
  if (canPromote) return "Available after successful candidate run.";
  return "Run candidate first.";
}

export default function CampaignActionBar({
  selectedCampaignId,
  onValidate,
  validateLoading,
  onRunCandidate,
  runCandidateLoading,
  canRunCandidate,
  onPromote,
  promoteLoading,
  canPromote,
  validateResult,
  updateModeWarnings,
  hasFailedCandidateRun,
}) {
  const runDisabled =
    !selectedCampaignId || !canRunCandidate || runCandidateLoading || promoteLoading;
  const runTitle = runCandidateButtonTitle({ validateResult, runCandidateLoading, canRunCandidate });
  const promoteDisabled =
    !selectedCampaignId || !canPromote || promoteLoading || runCandidateLoading;
  const promoteTitle = promoteButtonTitle({
    validateResult,
    promoteLoading,
    runCandidateLoading,
    canPromote,
    hasFailedCandidateRun,
  });

  return (
    <div className="campaign-action-bar-wrap">
      <div className="campaign-action-flow">
        <div className="campaign-action-step">
          <button
            type="button"
            className="btn btn-primary"
            disabled={!selectedCampaignId || validateLoading || promoteLoading}
            onClick={onValidate}
            title="Check campaign readiness"
          >
            {validateLoading ? "Validating…" : "Validate"}
          </button>
          <p className="tiny subtle campaign-action-hint">
            {actionHintValidate({ validateResult, validateLoading })}
          </p>
        </div>
        <div className="campaign-action-step">
          <button
            type="button"
            className="btn btn-primary"
            disabled={runDisabled}
            title={runTitle}
            onClick={onRunCandidate}
          >
            {runCandidateLoading ? "Running candidate…" : "Run Candidate"}
          </button>
          <p className="tiny subtle campaign-action-hint">
            {actionHintCandidate({ validateResult, runCandidateLoading, canRunCandidate })}
          </p>
        </div>
        <div className="campaign-action-step">
          <button
            type="button"
            className="btn btn-primary"
            disabled={promoteDisabled}
            title={promoteTitle}
            onClick={onPromote}
          >
            {promoteLoading ? "Promoting…" : "Promote Primary"}
          </button>
          <p className="tiny subtle campaign-action-hint">
            {actionHintPromote({
              canPromote,
              promoteLoading,
              runCandidateLoading,
              hasFailedCandidateRun,
            })}
          </p>
        </div>
      </div>
      {updateModeWarnings?.length ? (
        <div className="campaign-logs-alert campaign-logs-alert-muted tiny campaign-action-ota-warn">
          OTA: {updateModeWarnings.join(" · ")} — Vehicle OTA state may need READY before candidate/promote. For demo,
          prepare OTA READY in the terminal; this UI does not change state automatically.
        </div>
      ) : null}
    </div>
  );
}
