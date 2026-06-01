import CampaignExecutionResultPanel from './CampaignExecutionResultPanel.jsx';

export default function CampaignValidateResultPanel({
  result,
  error,
  loading,
}) {
  return (
    <CampaignExecutionResultPanel
      title="Campaign Validate Result"
      subtitle="Read-only preflight — no deploy or state changes."
      mode="validate"
      loadingLabel="Running campaign-validate…"
      result={result}
      error={error}
      loading={loading}
    />
  );
}
