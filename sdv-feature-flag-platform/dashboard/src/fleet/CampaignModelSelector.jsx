import { FLEET_MODELS, carsInModel, resolveDeploymentCarIds } from './campaignDeployTargets.js';

export default function CampaignModelSelector({
  selectedModels,
  onToggleModel,
  vehicles,
  disabled,
  vehiclesDisabled,
  selectedRegions,
  noCampaignSelected,
}) {
  const previewIds = resolveDeploymentCarIds({
    vehicles,
    regions: selectedRegions,
    models: selectedModels,
  });

  return (
    <div
      className={`card campaign-target-card campaign-model-card campaign-filter-card${noCampaignSelected ? ' campaign-target-card-muted campaign-deploy-section-inactive' : ''}`}
      aria-disabled={noCampaignSelected || undefined}
    >
      <div className="campaign-target-head campaign-filter-card-head">
        <h3>Deployment model</h3>
        <p className="tiny subtle campaign-filter-card-desc">
          Toggle IONIQ5 / IONIQ6 / KONA_EV (multi-select); combines with region.
        </p>
      </div>
      <div
        className="campaign-target-grid"
        role="group"
        aria-label="Deployment model"
      >
        {FLEET_MODELS.map((model) => {
          const active = selectedModels.includes(model);
          const count = carsInModel(vehicles, model).length;
          return (
            <button
              key={model}
              type="button"
              className={`campaign-target-chip campaign-model-chip${active ? ' campaign-target-chip-active' : ''}`}
              disabled={disabled || vehiclesDisabled || noCampaignSelected}
              aria-pressed={active}
              onClick={() => onToggleModel(model)}
            >
              {model}
              {count ? ` (${count})` : ''}
            </button>
          );
        })}
      </div>
      <div className="campaign-filter-card-foot">
        {selectedModels.length ? (
          <p className="tiny subtle campaign-filter-card-status">
            <span className="mono">{selectedModels.join(', ')}</span>
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
              ? 'Select a campaign row above to filter by model.'
              : vehiclesDisabled
                ? 'Disabled — individual vehicles or deployment group selected.'
                : 'Optional: IONIQ5, IONIQ6, KONA_EV.'}
          </p>
        )}
      </div>
    </div>
  );
}
