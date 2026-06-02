import {
  FLEET_REGIONS,
  carsInRegion,
  resolveDeploymentCarIds,
} from './campaignDeployTargets.js';

export default function CampaignGroupSelector({
  selectedRegions,
  onToggleRegion,
  vehicles,
  disabled,
  vehiclesDisabled,
  selectedModels,
  noCampaignSelected,
}) {
  const previewIds = resolveDeploymentCarIds({
    vehicles,
    regions: selectedRegions,
    models: selectedModels,
  });

  return (
    <div
      className={`card campaign-target-card campaign-group-card campaign-filter-card${noCampaignSelected ? ' campaign-target-card-muted campaign-deploy-section-inactive' : ''}`}
      aria-disabled={noCampaignSelected || undefined}
    >
      <div className="campaign-target-head campaign-filter-card-head">
        <h3>Deployment region</h3>
        <p className="tiny subtle campaign-filter-card-desc">
          Toggle KR / US / EU (multi-select); combines with model filter.
        </p>
      </div>
      <div
        className="campaign-target-grid"
        role="group"
        aria-label="Deployment region"
      >
        {FLEET_REGIONS.map((region) => {
          const active = selectedRegions.includes(region);
          const count = carsInRegion(vehicles, region).length;
          return (
            <button
              key={region}
              type="button"
              className={`campaign-target-chip campaign-group-chip${active ? ' campaign-target-chip-active' : ''}`}
              disabled={disabled || vehiclesDisabled || noCampaignSelected}
              aria-pressed={active}
              onClick={() => onToggleRegion(region)}
            >
              {region}
              {count ? ` (${count})` : ''}
            </button>
          );
        })}
      </div>
      <div className="campaign-filter-card-foot">
        {selectedRegions.length ? (
          <p className="tiny subtle campaign-filter-card-status">
            <span className="mono">{selectedRegions.join(', ')}</span>
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
              ? 'Select a campaign row above to filter by region.'
              : vehiclesDisabled
                ? 'Disabled — individual vehicles or deployment group selected.'
                : 'Optional: KR, US, EU.'}
          </p>
        )}
      </div>
    </div>
  );
}
