import { FLEET_REGIONS, carsInRegion } from './campaignDeployTargets.js';

export default function CampaignGroupSelector({
  selectedRegion,
  onSelectRegion,
  vehicles,
  disabled,
  vehiclesDisabled,
}) {
  const regionCars = selectedRegion ? carsInRegion(vehicles, selectedRegion) : [];

  return (
    <div className="card campaign-target-card campaign-group-card">
      <div className="campaign-target-head">
        <h3>Deployment group</h3>
        <p className="tiny subtle">
          Deploy to every vehicle in the region (from fleet registry). Click the
          same region again to deselect. Cleared when you pick individual
          vehicles above.
        </p>
      </div>
      <div
        className="campaign-target-grid"
        role="group"
        aria-label="Deployment group region"
      >
        {FLEET_REGIONS.map((region) => {
          const active = selectedRegion === region;
          const count = carsInRegion(vehicles, region).length;
          return (
            <button
              key={region}
              type="button"
              className={`campaign-target-chip campaign-group-chip${active ? ' campaign-target-chip-active' : ''}`}
              disabled={disabled || vehiclesDisabled}
              aria-pressed={active}
              onClick={() => onSelectRegion(region)}
            >
              {region}
              {active && count ? ` (${count})` : ''}
            </button>
          );
        })}
      </div>
      {selectedRegion ? (
        <p className="tiny subtle">
          Region <span className="mono">{selectedRegion}</span> →{' '}
          <span className="mono">{regionCars.join(', ') || '—'}</span>
        </p>
      ) : (
        <p className="tiny campaign-target-hint-warn">
          {vehiclesDisabled
            ? 'Individual vehicles selected — region group is disabled.'
            : 'Optional: select KR, US, or EU for a regional rollout.'}
        </p>
      )}
    </div>
  );
}
