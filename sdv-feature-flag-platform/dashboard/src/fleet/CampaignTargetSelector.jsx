import {
  ALL_CAR_IDS,
  allCarsSelected,
  sortCarIds,
} from './campaignDeployTargets.js';

function VehicleToggle({ carId, active, disabled, onToggle }) {
  return (
    <label
      className={`campaign-target-toggle${active ? ' campaign-target-toggle-on' : ''}${disabled ? ' campaign-target-toggle-disabled' : ''}`}
    >
      <span className="campaign-target-toggle-label mono">{carId}</span>
      <span className="campaign-target-toggle-track" aria-hidden="true">
        <span className="campaign-target-toggle-thumb" />
      </span>
      <input
        type="checkbox"
        role="switch"
        className="campaign-target-toggle-input"
        checked={active}
        disabled={disabled}
        aria-label={`${carId} promote 대상`}
        onChange={() => onToggle(carId)}
      />
    </label>
  );
}

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
          토글로 Promote 대상 차량을 선택하세요. 선택한 차량마다 pygame에서
          예/아니오 확인창이 순서대로 표시됩니다. ALL로 전체 선택/해제.
          Region·Model·Group 필터 선택 시 차량 토글은 비활성화됩니다.
        </p>
      </div>
      <div
        className="campaign-target-toggle-grid"
        role="group"
        aria-label="Deployment target vehicles"
      >
        {ALL_CAR_IDS.map((carId) => (
          <VehicleToggle
            key={carId}
            carId={carId}
            active={selectedCarIds.includes(carId)}
            disabled={disabled || filterActive || noCampaignSelected}
            onToggle={onToggleCar}
          />
        ))}
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
          Promote pygame 확인 대상 ({sorted.length}):{' '}
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
