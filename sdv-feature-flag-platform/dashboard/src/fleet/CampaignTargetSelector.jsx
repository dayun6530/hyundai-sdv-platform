import {
  ALL_CAR_IDS,
  allCarsSelected,
  sortCarIds,
} from './campaignDeployTargets.js';

export default function CampaignTargetSelector({
  selectedCarIds,
  onToggleCar,
  onToggleAll,
  disabled,
  filterActive,
  noCampaignSelected,
}) {
  const allSelected = allCarsSelected(selectedCarIds);
  const sorted = sortCarIds(selectedCarIds);

  return (
    <div
      className={`card campaign-target-card${filterActive || noCampaignSelected ? ' campaign-target-card-muted' : ''}${noCampaignSelected ? ' campaign-deploy-section-inactive' : ''}`}
      aria-disabled={noCampaignSelected || undefined}
    >
      <div className="campaign-target-head">
        <h3>Deployment target</h3>
        <p className="tiny subtle">
          Select one or more vehicles (click again to deselect). Use ALL to
          toggle every car. Cleared when Deployment region, model, or group
          filters are selected.
        </p>
      </div>
      <div
        className="campaign-target-grid"
        role="group"
        aria-label="Deployment target vehicles"
      >
        {ALL_CAR_IDS.map((carId) => {
          const active = selectedCarIds.includes(carId);
          return (
            <button
              key={carId}
              type="button"
              className={`campaign-target-chip${active ? ' campaign-target-chip-active' : ''}`}
              disabled={disabled || filterActive || noCampaignSelected}
              aria-pressed={active}
              onClick={() => onToggleCar(carId)}
            >
              {carId}
            </button>
          );
        })}
        <button
          type="button"
          className={`campaign-target-chip campaign-target-chip-all${allSelected ? ' campaign-target-chip-active' : ''}`}
          disabled={disabled || filterActive || noCampaignSelected}
          aria-pressed={allSelected}
          onClick={onToggleAll}
        >
          ALL
        </button>
      </div>
      {sorted.length ? (
        <p className="tiny subtle">
          Selected ({sorted.length}):{' '}
          <span className="mono">{sorted.join(', ')}</span>
        </p>
      ) : (
        <p className="tiny campaign-target-hint-warn">
          {noCampaignSelected
            ? 'Select a campaign row above to configure deployment targets.'
            : filterActive
              ? 'Region/model filter selected — vehicle picks are disabled.'
              : 'Select at least one vehicle.'}
        </p>
      )}
    </div>
  );
}
