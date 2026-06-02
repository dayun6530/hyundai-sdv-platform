import {
  FLEET_ROLLOUT_GROUPS,
  carsInGroup,
  resolveDeploymentCarIds,
} from './campaignDeployTargets.js';

export default function CampaignRolloutGroupSelector({
  selectedGroup,
  onSelectGroup,
  rolloutProgress,
  vehicles,
  disabled,
  filterDisabled,
  noCampaignSelected,
}) {
  const previewIds = selectedGroup
    ? resolveDeploymentCarIds({ vehicles, group: selectedGroup })
    : [];

  return (
    <div
      className={`card campaign-target-card campaign-rollout-group-card campaign-filter-card${noCampaignSelected ? ' campaign-target-card-muted campaign-deploy-section-inactive' : ''}`}
      aria-disabled={noCampaignSelected || undefined}
    >
      <div className="campaign-target-head campaign-filter-card-head">
        <h3>Deployment group</h3>
        <p className="tiny subtle campaign-filter-card-desc">
          Toggle canary → beta → stable (click again to deselect). Each stage
          unlocks after promote on the previous group.
        </p>
      </div>
      <div
        className="campaign-target-grid campaign-rollout-group-grid"
        role="group"
        aria-label="Deployment group"
      >
        {FLEET_ROLLOUT_GROUPS.map((group) => {
          const prog = rolloutProgress?.[group] || {};
          const unlocked = Boolean(prog.unlocked);
          const complete = Boolean(prog.complete);
          const active = selectedGroup === group;
          const count = carsInGroup(vehicles, group).length;
          const label = group.charAt(0).toUpperCase() + group.slice(1);
          return (
            <button
              key={group}
              type="button"
              className={`campaign-target-chip campaign-rollout-group-chip${active ? ' campaign-target-chip-active' : ''}${complete ? ' campaign-rollout-group-complete' : ''}${!unlocked ? ' campaign-rollout-group-locked' : ''}`}
              disabled={disabled || filterDisabled || noCampaignSelected || !unlocked}
              aria-pressed={active}
              title={
                !unlocked
                  ? `Complete promote on the previous stage first`
                  : complete
                    ? 'Promote completed for this group'
                    : undefined
              }
              onClick={() => onSelectGroup(group)}
            >
              {label}
              {complete ? ' ✓' : ''}
              {count ? ` (${count})` : ''}
            </button>
          );
        })}
      </div>
      <div className="campaign-filter-card-foot">
        {selectedGroup ? (
          <p className="tiny subtle campaign-filter-card-status">
            <span className="mono">{selectedGroup}</span>
            {previewIds.length ? (
              <>
                {' '}
                → <span className="mono">{previewIds.join(', ')}</span>
              </>
            ) : null}
          </p>
        ) : (
          <p className="tiny campaign-target-hint-warn campaign-filter-card-status">
            {noCampaignSelected
              ? 'Select a campaign row above to use staged rollout groups.'
              : filterDisabled
                ? 'Disabled — region, model, or individual vehicles selected.'
                : 'Optional: canary, beta, or stable (canary enabled first).'}
          </p>
        )}
      </div>
    </div>
  );
}
