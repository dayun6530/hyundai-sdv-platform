import { useCallback, useEffect, useState } from "react";
import {
  fetchVehicleSafetyState,
  prepareVehicleDemoReady,
  updateVehicleSafetyState,
} from "../api/campaigns.js";
import { getOtaSafetyChecklist } from "./fleetDashboardModel.js";
import { RawJsonDetails, StatusBadge } from "./campaignShared.jsx";

const UPDATE_MODE_OPTIONS = ["READY", "COMPLETED", "FAILED", "DOWNLOADING", "READY_TO_INSTALL", "INSTALLING"];
const DOWNLOAD_OPTIONS = ["not_started", "downloading", "downloaded", "failed"];
const INSTALL_OPTIONS = ["not_started", "installing", "installed", "failed"];

function badgeForDemoReady(ready) {
  return ready ? "status-green" : "status-red";
}

async function fetchFleetStateForCar(carId) {
  const id = encodeURIComponent(String(carId || "").trim());
  try {
    const res = await fetch(`/api/fleet/state/${id}`, { cache: "no-store" });
    const data = await res.json().catch(() => ({}));
    if (!res.ok || !data.ok) return null;
    return data.state || null;
  } catch {
    return null;
  }
}

export default function VehicleSafetyPreconditionsCard({
  carId,
  feature = "ADAS",
  onChanged,
  defaultOpen = false,
}) {
  const [open, setOpen] = useState(defaultOpen);
  const [safety, setSafety] = useState(null);
  const [draft, setDraft] = useState(null);
  const [loading, setLoading] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState("");
  const [fleetState, setFleetState] = useState(null);

  const load = useCallback(async () => {
    if (!carId) {
      setSafety(null);
      setDraft(null);
      setFleetState(null);
      return;
    }
    setLoading(true);
    setError("");
    const [res, st] = await Promise.all([fetchVehicleSafetyState(carId, feature), fetchFleetStateForCar(carId)]);
    setFleetState(st);
    if (!res.ok) {
      setSafety(null);
      setDraft(null);
      setError(res.error || "Failed to load safety state");
    } else {
      setSafety(res);
      setDraft({
        update_mode: res.ota?.update_mode || "",
        download_status: res.ota?.download_status || "",
        install_status: res.ota?.install_status || "",
        driver_approved: Boolean(res.ota?.driver_approved),
      });
    }
    setLoading(false);
  }, [carId, feature]);

  useEffect(() => {
    load();
  }, [load]);

  const applyResult = async (res) => {
    if (!res.ok) {
      setError(res.error || "Safety update failed");
      return;
    }
    setError("");
    setSafety(res);
    setDraft({
      update_mode: res.ota?.update_mode || "",
      download_status: res.ota?.download_status || "",
      install_status: res.ota?.install_status || "",
      driver_approved: Boolean(res.ota?.driver_approved),
    });
    await onChanged?.(res);
  };

  const saveDraft = async () => {
    if (!carId || !draft) return;
    setSaving(true);
    const res = await updateVehicleSafetyState(carId, draft, feature);
    await applyResult(res);
    setSaving(false);
  };

  const onPrepareDemoReady = async () => {
    if (!carId) return;
    setSaving(true);
    const res = await prepareVehicleDemoReady(carId, feature);
    await applyResult(res);
    setSaving(false);
  };

  if (!carId) {
    return (
      <div className="card campaign-safety-card">
        <h3>Safety Preconditions</h3>
        <p className="tiny subtle">
          Select deployment target (vehicle or region) above to edit OTA demo
          safety. For multi-vehicle targets, the first selected car is shown
          here; Promote runs Prepare Demo READY on all targets automatically.
        </p>
      </div>
    );
  }

  const fleetChecklist = fleetState ? getOtaSafetyChecklist(fleetState.missing ? null : fleetState) : null;
  const demoReady = safety?.demo_ready;
  const demoLabel = safety?.demo_ready_label || (demoReady ? "YES" : "NO");

  return (
    <details className="card campaign-safety-card" open={open} onToggle={(e) => setOpen(e.target.open)}>
      <summary className="campaign-safety-summary">
        <div className="campaign-safety-summary-head">
          <div>
            <h3>Safety Preconditions</h3>
            <p className="tiny subtle">
              Demo safety controls — prepares <code className="mono">deploy/state/{carId}.json</code> for campaign
              execution (does not run deploy).
            </p>
          </div>
          <div className="vd-badge-row">
            <StatusBadge text={`Demo Ready: ${demoLabel}`} className={badgeForDemoReady(demoReady)} />
            {safety?.ota?.update_mode ? (
              <StatusBadge
                text={`update_mode ${safety.ota.update_mode}`}
                className={safety.ota.update_mode === "READY" ? "status-green" : "status-yellow"}
              />
            ) : null}
            {safety?.kill_switch ? <StatusBadge text="kill_switch ON" className="status-red" /> : null}
            {!safety?.ota?.driver_approved && safety ? (
              <StatusBadge text="driver not approved" className="status-yellow" />
            ) : null}
            {fleetChecklist && fleetChecklist.level !== "READY" ? (
              <StatusBadge text={`Fleet gate: ${fleetChecklist.rolloutReadiness}`} className="status-yellow" />
            ) : null}
          </div>
        </div>
      </summary>

      {loading ? <p className="tiny subtle">Loading safety state…</p> : null}
      {error ? <div className="campaign-logs-alert campaign-logs-alert-error tiny">{error}</div> : null}

      {safety && !demoReady && safety.demo_ready_reasons?.length ? (
        <ul className="warning-list tiny" style={{ marginTop: 8 }}>
          {safety.demo_ready_reasons.map((r) => (
            <li key={r}>{r}</li>
          ))}
        </ul>
      ) : null}

      <div className="campaign-safety-actions">
        <button type="button" className="btn btn-primary" disabled={saving} onClick={onPrepareDemoReady}>
          {saving ? "Applying…" : "Prepare Demo READY"}
        </button>
        <button type="button" className="btn" disabled={saving || loading} onClick={load}>
          Refresh
        </button>
        <span className="tiny subtle">
          Sets OTA fields in state JSON only (not version_map / campaign package). Re-runs Validate for the selected
          campaign automatically.
        </span>
      </div>

      {draft ? (
        <div className="campaign-safety-form">
          <label className="campaign-safety-field">
            <span>update_mode</span>
            <select
              className="select mono"
              value={draft.update_mode}
              onChange={(e) => setDraft((d) => ({ ...d, update_mode: e.target.value }))}
            >
              {UPDATE_MODE_OPTIONS.map((v) => (
                <option key={v} value={v}>
                  {v}
                </option>
              ))}
            </select>
          </label>
          <label className="campaign-safety-field">
            <span>download_status</span>
            <select
              className="select mono"
              value={draft.download_status}
              onChange={(e) => setDraft((d) => ({ ...d, download_status: e.target.value }))}
            >
              {DOWNLOAD_OPTIONS.map((v) => (
                <option key={v} value={v}>
                  {v}
                </option>
              ))}
            </select>
          </label>
          <label className="campaign-safety-field">
            <span>install_status</span>
            <select
              className="select mono"
              value={draft.install_status}
              onChange={(e) => setDraft((d) => ({ ...d, install_status: e.target.value }))}
            >
              {INSTALL_OPTIONS.map((v) => (
                <option key={v} value={v}>
                  {v}
                </option>
              ))}
            </select>
          </label>
          <label className="campaign-safety-field campaign-safety-check">
            <input
              type="checkbox"
              checked={draft.driver_approved}
              onChange={(e) => setDraft((d) => ({ ...d, driver_approved: e.target.checked }))}
            />
            <span>driver_approved</span>
          </label>
          <button type="button" className="btn btn-primary" disabled={saving} onClick={saveDraft}>
            {saving ? "Saving…" : "Apply changes"}
          </button>
        </div>
      ) : null}

      {safety?.extra ? (
        <details className="campaign-raw-details" style={{ marginTop: 10 }}>
          <summary className="tiny subtle">Other OTA / vehicle signals (read-only)</summary>
          <dl className="campaign-kv campaign-kv-wide">
            {Object.entries(safety.extra).map(([k, v]) => (
              <div key={k} style={{ display: "contents" }}>
                <dt>{k}</dt>
                <dd className="mono tiny">{String(v)}</dd>
              </div>
            ))}
          </dl>
        </details>
      ) : null}

      {fleetChecklist?.items?.length ? (
        <details className="campaign-raw-details" style={{ marginTop: 8 }}>
          <summary className="tiny subtle">Fleet deploy safety checklist</summary>
          <ul className="campaign-validate-items tiny">
            {fleetChecklist.items.map((item) => (
              <li key={item.id} className={item.pass ? "campaign-validate-item-ok" : "campaign-validate-item-warn"}>
                <StatusBadge text={item.pass ? "OK" : "WARN"} className={item.pass ? "status-green" : "status-yellow"} />
                <span>
                  {item.label} — {item.detail}
                </span>
              </li>
            ))}
          </ul>
        </details>
      ) : null}

      <RawJsonDetails label="Raw deploy/state ota (full file in Advanced)" data={fleetState} />
    </details>
  );
}
