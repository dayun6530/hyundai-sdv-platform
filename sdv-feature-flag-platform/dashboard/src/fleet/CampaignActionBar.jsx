function runCandidateButtonTitle({
  validateResult,
  runCandidateLoading,
  canRunCandidate,
}) {
  if (runCandidateLoading) return 'Running candidate…';
  if (!validateResult) return 'Validate first';
  if ((validateResult.summary?.fail ?? 0) > 0) return 'Fix validation failures';
  if (canRunCandidate) return 'Run Candidate';
  return 'Validate first';
}

function promoteButtonTitle({
  validateResult,
  promoteLoading,
  runCandidateLoading,
  canPromote,
  hasFailedCandidateRun,
}) {
  if (promoteLoading) return 'Promoting…';
  if (!validateResult) return 'Validate first';
  if ((validateResult.summary?.fail ?? 0) > 0) return 'Fix validation failures';
  if (runCandidateLoading) return 'Run candidate first';
  if (hasFailedCandidateRun && !canPromote)
    return 'Candidate must succeed first';
  if (!canPromote) return 'Run candidate first';
  return 'Promote Primary';
}

function firstValidateFailMessage(validateResult) {
  const items = validateResult?.items || [];
  const fails = items.filter((i) => i.level === 'FAIL');
  if (!fails.length) return '';
  const head = String(fails[0].message || '').trim();
  if (fails.length === 1) return head;
  return `${head} (+${fails.length - 1} more)`;
}

function actionHintValidate({
  validateResult,
  validateLoading,
  targetVehicleCount,
}) {
  if (validateLoading) return 'Running campaign-validate…';
  if (!validateResult) return 'Check campaign readiness before deploy steps.';
  const fail = validateResult.summary?.fail ?? 0;
  if (fail === 0 && targetVehicleCount > 1) {
    return `fail=0 (${targetVehicleCount} vehicles)`;
  }
  if (fail > 0) {
    const detail = firstValidateFailMessage(validateResult);
    if (/version_map/i.test(detail)) {
      return `FAIL: ${detail} — re-select the campaign (auto-syncs version_map) or align deploy/version_map.json slot B manually.`;
    }
    return detail ? `FAIL: ${detail}` : 'Fix FAIL items before Run Candidate.';
  }
  return 'fail=0 — Run Candidate is available.';
}

function actionHintCandidate({
  validateResult,
  runCandidateLoading,
  canRunCandidate,
  deploymentHasTarget,
  imageOk,
  targetVehicleCount,
}) {
  if (runCandidateLoading) return 'Deploying candidate via campaign-run…';
  if (!deploymentHasTarget) return 'Select deployment target first.';
  if (!imageOk) return 'Local Docker image missing — build and push first.';
  if (!validateResult) return 'Validate first.';
  if ((validateResult.summary?.fail ?? 0) > 0)
    return 'Fix validation failures first.';
  if (canRunCandidate) {
    return targetVehicleCount > 1
      ? `Deploys candidate to ${targetVehicleCount} vehicles (sequential; shadow may update per car).`
      : 'Deploys candidate container (Docker runtime).';
  }
  return 'Validate first.';
}

function promoteLoadingLabel(promotePhase) {
  if (promotePhase === 'campaign') return 'Running campaign-run promote…';
  if (promotePhase === 'docker') return 'Applying Docker pending queue…';
  if (promotePhase === 'apply-native') return 'Applying native rollout (apply-pending-native)…';
  if (promotePhase === 'verify') return 'Waiting for vehicle_runtime JSON to match…';
  return 'Promoting candidate to primary…';
}

function actionHintPromote({
  canPromote,
  promoteLoading,
  promotePhase,
  promoteCompleteMessage,
  runCandidateLoading,
  hasFailedCandidateRun,
  deploymentHasTarget,
  imageOk,
  targetVehicleCount,
  candidateStagedInRuntime,
}) {
  if (promoteCompleteMessage && !promoteLoading) return promoteCompleteMessage;
  if (promoteLoading) return promoteLoadingLabel(promotePhase);
  if (runCandidateLoading) return 'Wait for candidate run to finish.';
  if (!deploymentHasTarget) return 'Select deployment target first.';
  if (!imageOk) return 'Local Docker image missing.';
  if (hasFailedCandidateRun) return 'Latest candidate run must succeed.';
  if (canPromote && candidateStagedInRuntime) {
    return targetVehicleCount > 1
      ? `Candidate already staged on all ${targetVehicleCount} vehicles (vehicle_runtime).`
      : 'Candidate version already staged in vehicle_runtime.';
  }
  if (canPromote) {
    return targetVehicleCount > 1
      ? `Promote after candidate success on all ${targetVehicleCount} targets.`
      : 'Available after successful candidate run.';
  }
  return 'Run candidate first, or stage candidate in vehicle_runtime.';
}

function actionHintDrop({
  dropCandidateLoading,
  deploymentHasTarget,
  targetVehicleCount,
}) {
  if (dropCandidateLoading) return 'Clearing candidate (shadow) on targets…';
  if (!deploymentHasTarget) return 'Select deployment target first.';
  return targetVehicleCount > 1
    ? `Clears shadow/candidate on all ${targetVehicleCount} vehicles (vehicle_runtime + fleet state).`
    : 'Clears shadow/candidate lane (vehicle_runtime + fleet state).';
}

export default function CampaignActionBar({
  selectedCampaignId,
  actionsEnabled,
  onValidate,
  validateLoading,
  onRunCandidate,
  runCandidateLoading,
  canRunCandidate,
  onPromote,
  promoteLoading,
  promotePhase,
  promoteCompleteMessage,
  canPromote,
  onDropCandidate,
  dropCandidateLoading,
  canDropCandidate,
  validateResult,
  updateModeWarnings,
  hasFailedCandidateRun,
  deploymentHasTarget,
  targetVehicleCount,
  imageAvailable,
  candidateStagedInRuntime,
}) {
  const enabled =
    actionsEnabled !== undefined ? actionsEnabled : Boolean(selectedCampaignId);
  const imageOk = imageAvailable !== false;
  const runDisabled =
    !enabled ||
    !deploymentHasTarget ||
    !canRunCandidate ||
    !imageOk ||
    runCandidateLoading ||
    promoteLoading ||
    dropCandidateLoading;
  const runTitle = runCandidateButtonTitle({
    validateResult,
    runCandidateLoading,
    canRunCandidate,
  });
  const promoteDisabled =
    !enabled ||
    !deploymentHasTarget ||
    !canPromote ||
    !imageOk ||
    promoteLoading ||
    runCandidateLoading ||
    dropCandidateLoading;
  const dropDisabled =
    !enabled ||
    !deploymentHasTarget ||
    !canDropCandidate ||
    promoteLoading ||
    runCandidateLoading ||
    dropCandidateLoading ||
    validateLoading;
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
            disabled={
              !enabled ||
              !deploymentHasTarget ||
              validateLoading ||
              promoteLoading ||
              dropCandidateLoading
            }
            onClick={onValidate}
            title="Check campaign readiness"
          >
            {validateLoading ? 'Validating…' : 'Validate'}
          </button>
          <p className="tiny subtle campaign-action-hint">
            {!deploymentHasTarget
              ? 'Select deployment target (vehicle or region) first.'
              : actionHintValidate({
                  validateResult,
                  validateLoading,
                  targetVehicleCount,
                })}
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
            {runCandidateLoading ? 'Running candidate…' : 'Run Candidate'}
          </button>
          <p className="tiny subtle campaign-action-hint">
            {actionHintCandidate({
              validateResult,
              runCandidateLoading,
              canRunCandidate,
              deploymentHasTarget,
              imageOk,
              targetVehicleCount,
            })}
          </p>
        </div>
        <div className="campaign-action-step campaign-action-step-promote">
          <div className="campaign-action-promote-row">
            <button
              type="button"
              className="btn btn-primary"
              disabled={promoteDisabled}
              title={promoteTitle}
              onClick={onPromote}
            >
              {promoteLoading ? 'Promoting…' : 'Promote Primary'}
            </button>
            <button
              type="button"
              className="btn btn-secondary"
              disabled={dropDisabled}
              title="Clear candidate (shadow) on deployment targets"
              onClick={onDropCandidate}
            >
              {dropCandidateLoading ? 'Dropping…' : 'Drop Candidate'}
            </button>
          </div>
          <p className="tiny subtle campaign-action-hint">
            {actionHintPromote({
              canPromote,
              promoteLoading,
              promotePhase,
              promoteCompleteMessage,
              runCandidateLoading,
              hasFailedCandidateRun,
              deploymentHasTarget,
              imageOk,
              targetVehicleCount,
              candidateStagedInRuntime,
            })}
          </p>
          <p className="tiny subtle campaign-action-hint">
            {actionHintDrop({
              dropCandidateLoading,
              deploymentHasTarget,
              targetVehicleCount,
            })}
          </p>
        </div>
      </div>
      {updateModeWarnings?.length ? (
        <div className="campaign-logs-alert campaign-logs-alert-muted tiny campaign-action-ota-warn">
          OTA: {updateModeWarnings.join(' · ')} — Validate / Run Candidate /
          Promote apply Prepare Demo READY automatically before each step.
        </div>
      ) : null}
    </div>
  );
}
