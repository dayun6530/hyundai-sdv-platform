import { useEffect, useState } from "react";
import { previewFleetCommand, applyKillSwitch, runFleetAction } from "../api/fleet.js";
import RollbackPanel from "./RollbackPanel.jsx";

const FEATURES = ["ADAS", "Navigation", "Energy"];
const FEATURE_LABELS = {
  ADAS: "ADAS",
  Navigation: "Navigation",
  Energy: "EV Energy",
};

const DEFAULT_KILL_REASON = "manual kill switch from dashboard";
const DEFAULT_CLEAR_REASON = "manual clear kill switch from dashboard";

const KILL_POLICY = {
  ADAS:
    "Emergency disable + fallback to stable Primary. ADAS flag becomes OFF.",
  Navigation:
    "Basic guidance remains ON. Realtime/charging recommendation flags are turned OFF and slot falls back to A.",
  Energy:
    "Essential battery functions remain ON. Advanced range/charging recommendation flags are turned OFF and slot falls back to A.",
};

export default function VehicleActionsSection({
  carId,
  missing,
  state,
  onFleetRefresh,
  onRollbackMetaInvalidate,
}) {
  const [opFeature, setOpFeature] = useState("ADAS");
  const [syncRollback, setSyncRollback] = useState(true);
  const [syncFlag, setSyncFlag] = useState(false);
  const [syncKill, setSyncKill] = useState(true);
  const [killReason, setKillReason] = useState("");
  const [busy, setBusy] = useState(null);
  const [actionErr, setActionErr] = useState("");
  const [lastResult, setLastResult] = useState(null);
  const [previewBusy, setPreviewBusy] = useState(null);
  const [previewErr, setPreviewErr] = useState("");
  const [previewResult, setPreviewResult] = useState(null);

  const disabled = !carId || missing;
  const featState = state?.features?.[opFeature];
  const currentFlag = (featState?.flag || "—").toString().toUpperCase();
  const killOn = Boolean(featState?.kill_switch);

  const killPreviewReason = killReason.trim() || "manual preview from dashboard";
  const commandPreview = previewResult?.preview || null;
  const legacyCommandPreview = Array.isArray(commandPreview?.legacy_command_preview)
    ? commandPreview.legacy_command_preview
    : [];
  const commandPreviewNotes = Array.isArray(commandPreview?.notes) ? commandPreview.notes : [];

  useEffect(() => {
    setPreviewErr("");
    setPreviewResult(null);
  }, [carId, opFeature, killPreviewReason]);

  const applyResult = async (payload) => {
    const { busyKey, ...apiPayload } = payload;
    setBusy(busyKey || apiPayload.action);
    setActionErr("");
    try {
      const data = await runFleetAction(apiPayload);
      setLastResult({
        ...data,
        target_version: apiPayload.target_version,
        summary_json: data.summary_json,
      });
      await onFleetRefresh?.();
      onRollbackMetaInvalidate?.();
      if (!data.ok) {
        setActionErr(data.error || `DeploymentMgr exited with code ${data.exit_code ?? "?"}`);
      }
    } catch (e) {
      setActionErr(e.message || "Action failed");
      setLastResult(null);
    } finally {
      setBusy(null);
    }
  };

  const previewAction = async (label, payload) => {
    if (disabled || previewBusy) return;
    setPreviewBusy(label);
    setPreviewErr("");
    try {
      const data = await previewFleetCommand(payload);
      setPreviewResult({ label, request: payload, ...data });
      if (data?.success === false || data?.preview?.ok === false) {
        setPreviewErr(data?.error || data?.preview?.error || "command preview failed");
      }
    } catch (e) {
      setPreviewResult(null);
      setPreviewErr(e.message || "command preview failed");
    } finally {
      setPreviewBusy(null);
    }
  };

  const previewKillSwitch = () =>
    previewAction("kill", {
      action: "activate-kill-switch",
      vehicle_id: carId,
      feature: opFeature,
      reason: killPreviewReason,
    });

  const doFlag = (flag) => {
    if (disabled || busy) return;
    if (flag === "OFF") {
      const ok = window.confirm(
        "Set FLAG OFF? This is a normal administrative disable (not Kill Switch). Continue?",
      );
      if (!ok) return;
    }
    applyResult({
      action: "flag",
      car_id: carId,
      feature: opFeature,
      flag,
      sync_config: syncFlag,
    });
  };

  const doKill = () => {
    if (disabled || busy) return;
    const reason = killReason.trim() || DEFAULT_KILL_REASON;
    const ok = window.confirm(`Apply Kill Switch to ${opFeature} on ${carId}?\nReason: ${reason}`);
    if (!ok) return;
    applyKillResult({
      kill: true,
      reason,
      busyKey: "kill",
    });
  };

  const doClearKill = () => {
    if (disabled || busy || !killOn) return;
    const reason = killReason.trim() || DEFAULT_CLEAR_REASON;
    const ok = window.confirm(
      `Clear Kill Switch for ${opFeature} on ${carId}?\nThis updates deploy state only — it does not restart the Feature runtime.`,
    );
    if (!ok) return;
    applyKillResult({
      kill: false,
      reason,
      busyKey: "kill-clear",
    });
  };

  const applyKillResult = async ({ kill, reason, busyKey }) => {
    setBusy(busyKey);
    setActionErr("");
    try {
      const data = await applyKillSwitch({
        car_id: carId,
        feature: opFeature,
        kill,
        reason,
        source: "dashboard",
        send_runtime: kill,
        apply_fleet_kill_policy: kill,
        sync_config: syncKill,
      });
      setLastResult({
        ...data,
        action: kill ? "kill-switch" : "kill-switch-clear",
        ok: data.ok !== false,
      });
      await onFleetRefresh?.();
      onRollbackMetaInvalidate?.();
      if (!data.ok) {
        setActionErr(data.error || "Kill switch request failed");
      }
    } catch (e) {
      setActionErr(e.message || "Kill switch request failed");
      setLastResult(null);
    } finally {
      setBusy(null);
    }
  };

  return (
    <section className="vd-block va-section">
      <h3 className="vd-block-title">Operational Actions</h3>
      <p className="tiny subtle">
        Vehicle-scoped CLI only. <strong>FLAG OFF</strong> is a normal administrative disable. <strong>Kill Switch</strong> is an emergency disable for a reported issue.
      </p>

      {disabled ? (
        <div className="rollout-banner warn">
          Actions are disabled until deploy/state exists for this vehicle (not missing).
        </div>
      ) : null}

      <div className="va-feature-row">
        <label className="va-field">
          <span>Feature (flag / kill)</span>
          <select
            className="select"
            value={opFeature}
            onChange={(e) => setOpFeature(e.target.value)}
            disabled={disabled}
          >
            {FEATURES.map((f) => (
              <option key={f} value={f}>
                {FEATURE_LABELS[f]}
              </option>
            ))}
          </select>
        </label>
      </div>

      <div className="va-grid">
        <RollbackPanel
          carId={carId}
          state={state}
          disabled={disabled}
          syncRollback={syncRollback}
          onFleetRefresh={onFleetRefresh}
          onRollbackMetaInvalidate={onRollbackMetaInvalidate}
        />
        <label className="va-check" style={{ marginTop: 8 }}>
          <input type="checkbox" checked={syncRollback} onChange={(e) => setSyncRollback(e.target.checked)} disabled={disabled} />
          <span>
            <code className="mono">sync-config-car</code> after successful rollback
          </span>
        </label>

        <article className="va-card">
          <h4>Feature flag (fleet-flag)</h4>
          <p className="tiny">
            Current flag: <strong className="mono">{currentFlag}</strong>
          </p>
          <p className="tiny rollout-banner warn va-note">
            FLAG OFF is a normal administrative disable. Kill Switch is emergency disable due to an issue.
          </p>
          <label className="va-check">
            <input type="checkbox" checked={syncFlag} onChange={(e) => setSyncFlag(e.target.checked)} disabled={disabled} />
            <span>
              <code className="mono">--sync-config</code>
            </span>
          </label>
          <div className="va-btn-row">
            <button type="button" className="btn va-btn" disabled={disabled || busy} onClick={() => doFlag("ON")}>
              {busy === "flag" ? "Running…" : "Set FLAG ON"}
            </button>
            <button type="button" className="btn btn-danger va-btn" disabled={disabled || busy} onClick={() => doFlag("OFF")}>
              Set FLAG OFF
            </button>
          </div>
        </article>

        <article className="va-card">
          <h4>Kill Switch (integrated)</h4>
          <p className="tiny">
            Current kill_switch: <strong>{killOn ? "ON" : "OFF"}</strong>
            {featState?.kill_source ? (
              <span className="subtle"> · source {featState.kill_source}</span>
            ) : null}
          </p>
          {featState?.killed_at ? (
            <p className="tiny subtle">killed_at: <span className="mono">{featState.killed_at}</span></p>
          ) : null}
          {featState?.cleared_at ? (
            <p className="tiny subtle">cleared_at: <span className="mono">{featState.cleared_at}</span></p>
          ) : null}
          <p className="tiny subtle">{KILL_POLICY[opFeature]}</p>
          <label className="va-field">
            <span>reason</span>
            <input
              className="input"
              value={killReason}
              onChange={(e) => setKillReason(e.target.value)}
              placeholder="lane_detection_error"
              disabled={disabled}
            />
          </label>
          <label className="va-check">
            <input type="checkbox" checked={syncKill} onChange={(e) => setSyncKill(e.target.checked)} disabled={disabled} />
            <span>
              <code className="mono">--sync-config</code> (Apply Kill ON only)
            </span>
          </label>
          <div className="va-btn-row">
            <button type="button" className="btn btn-danger va-btn" disabled={disabled || busy} onClick={doKill}>
              {busy === "kill" ? "Applying Kill Switch…" : "Apply Kill Switch"}
            </button>
            <button
              type="button"
              className="btn va-btn"
              disabled={disabled || busy || !killOn}
              onClick={doClearKill}
            >
              {busy === "kill-clear" ? "Clearing…" : "Clear Kill Switch"}
            </button>
          </div>
          <p className="tiny rollout-banner warn va-note">
            Clear Kill Switch updates deploy state only. It does not automatically restart the Feature runtime.
          </p>
          <button
            type="button"
            className="btn btn-secondary va-btn"
            disabled={disabled || previewBusy}
            onClick={previewKillSwitch}
          >
            {previewBusy === "kill" ? "Previewing..." : "Preview Kill Switch"}
          </button>
        </article>
      </div>

      {previewErr ? <div className="error-banner va-err">{previewErr}</div> : null}

      {commandPreview ? (
        <div className="va-result">
          <div className="rollout-banner warn">
            <strong>This is a dry-run preview. No deployment command is executed.</strong>
          </div>
          <div className="va-result-head" style={{ marginTop: 10 }}>
            <span className="subtle">Vehicle Command Preview</span>
            <span className={`status-badge ${commandPreview.dry_run === true ? "status-green" : "status-red"}`}>
              dry_run: {String(commandPreview.dry_run)}
            </span>
            <span className={`status-badge ${commandPreview.will_execute === false ? "status-green" : "status-red"}`}>
              will_execute: {String(commandPreview.will_execute)}
            </span>
            <span className="mono tiny subtle">{previewResult?.label || commandPreview.action}</span>
          </div>
          <div className="rollout-readonly-grid" style={{ marginTop: 12 }}>
            <div>
              <div className="label">action</div>
              <div className="value mono">{commandPreview.action || "—"}</div>
            </div>
            <div>
              <div className="label">vehicle_id</div>
              <div className="value mono">{commandPreview.vehicle_id || "—"}</div>
            </div>
            <div>
              <div className="label">feature</div>
              <div className="value mono">{commandPreview.feature || "—"}</div>
            </div>
            {commandPreview.version ? (
              <div>
                <div className="label">version</div>
                <div className="value mono">{commandPreview.version}</div>
              </div>
            ) : null}
          </div>
          <div style={{ marginTop: 12 }}>
            <div className="label">legacy_command_preview</div>
            <div className="json-box mono">
              {legacyCommandPreview.length
                ? legacyCommandPreview
                    .map((item, idx) => {
                      const command = item.command || `${item.method || ""} ${item.endpoint || ""}`.trim();
                      const body = item.body ? `\n   body: ${JSON.stringify(item.body)}` : "";
                      const note = item.note ? `\n   note: ${item.note}` : "";
                      return `${idx + 1}. [${item.type || "legacy"}] ${command}${body}${note}`;
                    })
                    .join("\n\n")
                : "—"}
            </div>
          </div>
          {commandPreviewNotes.length ? (
            <div style={{ marginTop: 12 }}>
              <div className="label">notes</div>
              <div className="json-box mono">{commandPreviewNotes.map((n) => `- ${n}`).join("\n")}</div>
            </div>
          ) : null}
        </div>
      ) : null}

      {actionErr ? <div className="error-banner va-err">{actionErr}</div> : null}

      {lastResult ? (
        <div className="va-result">
          <div className="va-result-head">
            <span className="subtle">Vehicle Action Result</span>
            <span className={`status-badge ${lastResult.ok ? "status-green" : "status-red"}`}>
              {lastResult.ok ? "success" : "failed"}
            </span>
            <span className="mono tiny subtle">
              exit {lastResult.exit_code ?? "—"} · {lastResult.action}
              {lastResult.target_version ? ` · ${lastResult.target_version}` : ""}
            </span>
          </div>
          {lastResult.summary_json ? (
            <div style={{ marginTop: 10 }}>
              <div className="label">[SUMMARY_JSON]</div>
              <div className="json-box mono">{JSON.stringify(lastResult.summary_json, null, 2)}</div>
            </div>
          ) : null}
          {lastResult.stdout ? (
            <div style={{ marginTop: 10 }}>
              <div className="label">stdout</div>
              <div className="json-box mono">{lastResult.stdout}</div>
            </div>
          ) : null}
          {lastResult.stderr ? (
            <div style={{ marginTop: 10 }}>
              <div className="label">stderr</div>
              <div className="json-box mono">{lastResult.stderr}</div>
            </div>
          ) : null}
        </div>
      ) : null}
    </section>
  );
}
