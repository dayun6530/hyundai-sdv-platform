import { useCallback, useEffect, useState } from "react";
import {
  fetchFleetSafety,
  prepareFleetDemoReady,
  updateFleetSafety,
} from "../api/safety.js";
import { StatusBadge } from "./campaignShared.jsx";

function badgeForReady(ready) {
  return ready ? "status-green" : "status-red";
}

const EDITABLE_FIELDS = [
  { key: "speed", label: "Speed (km/h)", type: "number" },
  { key: "gear", label: "Gear", type: "text" },
  { key: "ignition", label: "Ignition", type: "text" },
  { key: "battery", label: "Battery (%)", type: "number" },
  { key: "network", label: "Network", type: "text" },
  { key: "update_mode", label: "update_mode", type: "text" },
];

export default function VehicleSafetyPreconditionsCard({
  carId,
  feature = "ADAS",
  onChanged,
  defaultOpen = false,
  prepareAction = "",
  refreshToken = 0,
}) {
  const [open, setOpen] = useState(defaultOpen);
  const [safety, setSafety] = useState(null);
  const [draft, setDraft] = useState(null);
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState("");

  const load = useCallback(async () => {
    if (!carId) {
      setSafety(null);
      setDraft(null);
      return;
    }
    setLoading(true);
    setError("");
    const res = await fetchFleetSafety(carId, feature);
    if (!res.ok) {
      setSafety(null);
      setDraft(null);
      setError(res.error || "Failed to load safety state");
    } else {
      setSafety(res);
      const v = res.snapshot?.values || {};
      setDraft({
        speed: v.speed ?? 0,
        gear: v.gear ?? "P",
        ignition: v.ignition ?? "OFF",
        battery: v.battery ?? 0,
        network: v.network ?? "DISCONNECTED",
        update_mode: v.update_mode ?? "",
      });
    }
    setLoading(false);
  }, [carId, feature]);

  useEffect(() => {
    load();
  }, [load, refreshToken]);

  const applyResult = async (res) => {
    if (!res.ok) {
      setError(res.error || res.message || "Safety update failed");
      return;
    }
    setError("");
    setSafety(res);
    const v = res.snapshot?.values || {};
    setDraft({
      speed: v.speed ?? 0,
      gear: v.gear ?? "P",
      ignition: v.ignition ?? "OFF",
      battery: v.battery ?? 0,
      network: v.network ?? "DISCONNECTED",
      update_mode: v.update_mode ?? "",
    });
    await onChanged?.(res);
  };

  const saveDraft = async () => {
    if (!carId || !draft) return;
    setSaving(true);
    const res = await updateFleetSafety(carId, { ...draft, source: "manual-demo" }, feature);
    await applyResult(res);
    setSaving(false);
  };

  const onPrepareDemoReady = async () => {
    if (!carId) return;
    setSaving(true);
    const res = await prepareFleetDemoReady(carId, feature, prepareAction);
    await applyResult(res);
    setSaving(false);
  };

  const prepareLabel =
    prepareAction === "promote"
      ? "Prepare Demo READY for Promote"
      : prepareAction === "rollback"
        ? "Prepare Demo READY for Rollback"
        : prepareAction === "candidate-deploy"
          ? "Prepare Demo READY for Candidate"
          : "Prepare Demo READY";

  if (!carId) {
    return (
      <div className="card campaign-safety-card">
        <h3>Safety Preconditions</h3>
        <p className="tiny subtle">
          Select a deployment target above to view OTA safety preconditions. Values are simulated from{" "}
          <code className="mono">deploy/state/*.json</code>, not live vehicle sensors.
        </p>
      </div>
    );
  }

  const ready = safety?.ready;
  const checks = safety?.checks || [];
  const failed = safety?.failed_checks || [];

  return (
    <details className="card campaign-safety-card" open={open} onToggle={(e) => setOpen(e.target.open)}>
      <summary className="campaign-safety-summary">
        <div className="campaign-safety-summary-head">
          <div>
            <h3>Safety Preconditions</h3>
            <p className="tiny subtle">
              {safety?.simulation_notice_ko ||
                "현재 안전 조건 값은 실제 차량 센서가 아니라 데모용 state JSON 값을 기반으로 시뮬레이션됩니다."}
            </p>
          </div>
          <div className="vd-badge-row">
            <StatusBadge text={ready ? "READY" : "NOT READY"} className={badgeForReady(ready)} />
            {safety?.source_label ? (
              <StatusBadge text={`Source: ${safety.source_label}`} className="status-blue" />
            ) : null}
          </div>
        </div>
      </summary>

      {loading ? <p className="tiny subtle">Loading safety state…</p> : null}
      {error ? <div className="campaign-logs-alert campaign-logs-alert-error tiny">{error}</div> : null}

      {!ready && failed.length ? (
        <div className="campaign-logs-alert campaign-logs-alert-error tiny" style={{ marginTop: 8 }}>
          Blocked checks: {failed.join(", ")}
        </div>
      ) : null}

      {checks.length ? (
        <ul className="campaign-validate-items tiny" style={{ marginTop: 10 }}>
          {checks.map((c) => (
            <li
              key={c.key}
              className={c.passed ? "campaign-validate-item-ok" : "campaign-validate-item-warn"}
            >
              <StatusBadge text={c.passed ? "PASS" : "FAIL"} className={c.passed ? "status-green" : "status-red"} />
              <span>
                <strong>{c.label}</strong>: {String(c.actual)} / required {c.required}
                {!c.passed && c.reason ? ` — ${c.reason}` : ""}
              </span>
            </li>
          ))}
        </ul>
      ) : null}

      <div className="campaign-safety-actions">
        <button type="button" className="btn btn-primary" disabled={saving} onClick={onPrepareDemoReady}>
          {saving ? "Applying…" : prepareLabel}
        </button>
        <button type="button" className="btn" disabled={saving || loading} onClick={load}>
          Refresh
        </button>
        <span className="tiny subtle">
          Updates <code className="mono">deploy/state/{carId}.json</code> safety fields only — does not change feature
          versions or campaign state.
        </span>
      </div>

      {draft ? (
        <div className="campaign-safety-form">
          {EDITABLE_FIELDS.map(({ key, label, type }) => (
            <label key={key} className={`campaign-safety-field${type === "checkbox" ? " campaign-safety-check" : ""}`}>
              {type === "checkbox" ? (
                <>
                  <input
                    type="checkbox"
                    checked={Boolean(draft[key])}
                    onChange={(e) => setDraft((d) => ({ ...d, [key]: e.target.checked }))}
                  />
                  <span>{label}</span>
                </>
              ) : (
                <>
                  <span>{label}</span>
                  <input
                    className="select mono"
                    type={type}
                    value={draft[key] ?? ""}
                    onChange={(e) =>
                      setDraft((d) => ({
                        ...d,
                        [key]: type === "number" ? Number(e.target.value) : e.target.value,
                      }))
                    }
                  />
                </>
              )}
            </label>
          ))}
          <button type="button" className="btn btn-primary" disabled={saving} onClick={saveDraft}>
            {saving ? "Saving…" : "Apply manual values"}
          </button>
        </div>
      ) : null}

      <details className="campaign-raw-details" style={{ marginTop: 10 }}>
        <summary className="tiny subtle">Future: vehicle signal source</summary>
        <p className="tiny subtle">
          Demo mode uses values stored in deploy/state JSON. In a real vehicle, this snapshot can be populated from
          vehicle signals or MQTT/Signal I/F, then passed through <code className="mono">normalizeSafetySnapshot()</code>{" "}
          — the same <code className="mono">evaluateSafetyPreconditions()</code> logic applies.
        </p>
      </details>
    </details>
  );
}
