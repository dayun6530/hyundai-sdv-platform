import CampaignPercentDropdown from './CampaignPercentDropdown.jsx';
import {
  allocateCarsByPercent,
  enrichPercentRows,
  featuresWithLocalImage,
  isPercentConfigValid,
  newPercentRowId,
  percentSum,
  versionsForFeature,
} from './campaignPercentDeploy.js';

export default function CampaignPercentDeployCard({
  rows,
  onChangeRows,
  campaigns,
  resolvedCarIds,
  disabled,
  shuffleSeed = 1,
  onReshuffle,
}) {
  const sum = percentSum(rows);
  const sumOk = Math.abs(sum - 100) <= 0.01;
  const enriched = enrichPercentRows(rows, campaigns);
  const valid = isPercentConfigValid(rows, campaigns);
  const segments = valid
    ? allocateCarsByPercent(resolvedCarIds, enriched, {
        random: true,
        seed: shuffleSeed,
      })
    : [];
  const featureOptions = featuresWithLocalImage(campaigns);

  const updateRow = (id, patch) => {
    onChangeRows(
      rows.map((r) => (r.id === id ? { ...r, ...patch } : r)),
    );
  };

  const addRow = () => {
    onChangeRows([
      ...rows,
      { id: newPercentRowId(), percent: 0, feature: '', version: '' },
    ]);
  };

  const removeRow = (id) => {
    if (rows.length <= 2) return;
    onChangeRows(rows.filter((r) => r.id !== id));
  };

  return (
    <div
      className={`card campaign-target-card campaign-percent-card campaign-filter-card${disabled ? ' campaign-target-card-muted campaign-percent-card-disabled' : ''}`}
      aria-disabled={disabled || undefined}
    >
      <div className="campaign-target-head campaign-filter-card-head">
        <h3>Deployment percent</h3>
        <p className="tiny subtle campaign-filter-card-desc">
          Split fleet by proportion; each row picks feature + version (local
          image OK only). Percentages must sum to 100. Vehicles are assigned
          at random — use Reshuffle for a new split.
        </p>
      </div>

      <div className="campaign-percent-rows">
        {rows.map((row) => {
              const versionOptions = versionsForFeature(campaigns, row.feature);
              const seg = segments.find((s) => s.id === row.id);
          return (
            <div key={row.id} className="campaign-percent-row">
              <label className="campaign-percent-percent-field">
                <span className="tiny subtle">%</span>
                <input
                  type="number"
                  min={0}
                  max={100}
                  className="campaign-percent-input"
                  value={row.percent}
                  disabled={disabled}
                  onChange={(e) =>
                    updateRow(row.id, {
                      percent: Number(e.target.value),
                    })
                  }
                />
              </label>
              <CampaignPercentDropdown
                label="Feature"
                value={row.feature || ''}
                options={featureOptions}
                formatOption={(f) => f}
                optionKey={(f) => f}
                placeholder="Feature"
                disabled={disabled}
                onChange={(feature) =>
                  updateRow(row.id, { feature, version: '' })
                }
              />
              <CampaignPercentDropdown
                label="Version"
                value={row.version || ''}
                options={versionOptions}
                formatOption={(v) => v}
                optionKey={(v) => v}
                placeholder={row.feature ? 'Version' : 'Select feature first'}
                disabled={disabled || !row.feature}
                onChange={(version) => updateRow(row.id, { version })}
              />
              <button
                type="button"
                className="btn btn-secondary campaign-percent-remove-btn"
                disabled={disabled || rows.length <= 2}
                title={
                  rows.length <= 2
                    ? 'At least two proportions required'
                    : 'Remove row'
                }
                onClick={() => removeRow(row.id)}
              >
                −
              </button>
              {seg?.carIds?.length ? (
                <p className="tiny subtle campaign-percent-row-preview mono">
                  → {seg.carIds.join(', ')}
                </p>
              ) : null}
            </div>
          );
        })}
      </div>

      <div className="campaign-percent-toolbar">
        <button
          type="button"
          className="btn btn-secondary"
          disabled={disabled}
          onClick={addRow}
        >
          + Add proportion
        </button>
        <button
          type="button"
          className="btn btn-secondary campaign-percent-reshuffle-btn"
          disabled={disabled || !valid}
          title="Pick a new random split (same percentages)"
          onClick={() => onReshuffle?.()}
        >
          Reshuffle
        </button>
        <span
          className={`tiny campaign-percent-sum${sumOk ? ' campaign-percent-sum-ok' : ' campaign-percent-sum-warn'}`}
        >
          Total: {sum}% {sumOk ? '' : '(must equal 100)'}
        </span>
      </div>

      <div className="campaign-filter-card-foot">
        {disabled ? (
          <p className="tiny campaign-target-hint-warn campaign-filter-card-status">
            Campaign selected — deselect a campaign row above to configure
            percent deploy.
          </p>
        ) : valid && resolvedCarIds.length ? (
          <p className="tiny subtle campaign-filter-card-status">
            {segments.map((s) => (
              <span key={s.id}>
                {s.percent}% {s.feature} {s.version}:{' '}
                <span className="mono">
                  {(s.carIds || []).join(', ') || '—'}
                </span>
                {' · '}
              </span>
            ))}
          </p>
        ) : (
          <p className="tiny campaign-target-hint-warn campaign-filter-card-status">
            {!resolvedCarIds.length
              ? 'Fleet targets apply when a campaign is selected; percent mode uses all vehicles.'
              : !sumOk
                ? 'Adjust percentages to total 100.'
                : 'Pick feature and version for each row (image OK campaigns only).'}
          </p>
        )}
      </div>
    </div>
  );
}
