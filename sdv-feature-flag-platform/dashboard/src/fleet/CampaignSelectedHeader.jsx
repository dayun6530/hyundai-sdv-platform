import { StatusBadge } from "./campaignShared.jsx";
import {
  campaignStatusClass,
  displayTargetRole,
  formatPolicyFlow,
  formatTargetSelector,
  truncateMiddle,
} from "./campaignUiModel.js";

export default function CampaignSelectedHeader({ campaign, policy, policyFlow, runtimeCarId }) {
  if (!campaign) return null;

  return (
    <div className="card campaign-selected-header">
      <div className="card-header">
        <div>
          <h3>{campaign.name}</h3>
          <p className="mono tiny subtle">{campaign.campaign_id}</p>
          {campaign.description ? <p className="tiny subtle">{campaign.description}</p> : null}
        </div>
      </div>
      <div className="vd-badge-row campaign-header-badges">
        <StatusBadge text={campaign.feature} className="status-blue" />
        <StatusBadge text={`target ${campaign.target_version}`} className="status-green" />
        <StatusBadge text={`role ${displayTargetRole(campaign.target_role)}`} className="status-blue" />
        <StatusBadge text={campaign.status || "—"} className={campaignStatusClass(campaign.status)} />
        {campaign.approval_required ? (
          <StatusBadge text="approval required" className="status-yellow" />
        ) : (
          <StatusBadge text="no approval" className="status-gray" />
        )}
      </div>
      <dl className="campaign-kv campaign-kv-wide campaign-header-kv">
        <dt>Target</dt>
        <dd className="tiny">{formatTargetSelector(campaign.target_selector)}</dd>
        {runtimeCarId ? (
          <>
            <dt>Vehicle</dt>
            <dd className="mono">{runtimeCarId}</dd>
          </>
        ) : null}
        <dt>Policy</dt>
        <dd className="tiny" title={campaign.rollout_policy_id || ""}>
          {policyFlow || formatPolicyFlow(policy?.stages) || campaign.rollout_policy_id || "—"}
        </dd>
        <dt>Docker image</dt>
        <dd className="mono tiny" title={campaign.docker_image || ""}>
          {truncateMiddle(campaign.docker_image, 56) || "—"}
        </dd>
      </dl>
    </div>
  );
}
