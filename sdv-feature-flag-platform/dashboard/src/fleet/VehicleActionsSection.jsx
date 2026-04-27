import { useMemo, useState } from "react";
import { parseSummaryJsonFromStdout, runFleetAction } from "../api/fleet.js";
import { getVersionMapSlotEntry } from "./rolloutModel.js";

const FEATURES = ["ADAS", "Navigation", "Energy"];

const KILL_POLICY = {
  ADAS:
    "Emergency disable + fallback to stable A slot. ADAS flag becomes OFF.",
  Navigation:
    "Basic guidance remains ON. Realtime/charging recommendation flags are turned OFF and slot falls back to A.",
  Energy:
    "Essential battery functions remain ON. Advanced range/charging recommendation flags are turned OFF and slot falls back to A.",
};

export default function VehicleActionsSection({
  carId,
  missing,
  state,
  rollback,
  versionMapRaw,
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

  const disabled = !carId || missing;
  const featState = state?.features?.[opFeature];
  const currentFlag = (featState?.flag || "—").toString().toUpperCase();
  const killOn = Boolean(featState?.kill_switch);
  const shadowOn = Boolean(featState?.shadow_enabled);
  const rbAvailable = Boolean(rollback && rollback[opFeature]);

  const shadowBVersion = useMemo(() => {
    const e = getVersionMapSlotEntry(versionMapRaw, opFeature, "B");
    return e?.version || "—";
  }, [versionMapRaw, opFeature]);

  const applyResult = async (payload) => {
    setBusy(payload.action);
    setActionErr("");
    try {
      const data = await runFleetAction(payload);
      setLastResult({
        ...data,
        summary_json: data.summary_json ?? parseSummaryJsonFromStdout(data.stdout),
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

  const doRollback = () => {
    if (disabled || !rbAvailable || busy) return;
    const ok = window.confirm(
      `Rollback ${carId} ${opFeature} using deploy/rollback/${carId}/${opFeature}.json?`,
    );
    if (!ok) return;
    applyResult({
      action: "rollback",
      car_id: carId,
      feature: opFeature,
      sync_config: syncRollback,
    });
  };

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
    const reason = killReason.trim();
    if (!reason) {
      setActionErr("Kill reason is required (single token, e.g. lane_detection_error).");
      return;
    }
    if (!/^[a-zA-Z0-9_-]+$/.test(reason)) {
      setActionErr("Reason must be a single token: letters, digits, underscore, hyphen only.");
      return;
    }
    const ok = window.confirm(`Kill switch ${carId} ${opFeature} with reason "${reason}"?`);
    if (!ok) return;
    applyResult({
      action: "kill",
      car_id: carId,
      feature: opFeature,
      reason,
      sync_config: syncKill,
    });
  };

  const doShadow = (shadow_action) => {
    if (disabled || busy) return;
    const ok = window.confirm(`Set Shadow Mode ${shadow_action} for ${carId} ${opFeature}?`);
    if (!ok) return;
    applyResult({
      action: "shadow",
      car_id: carId,
      feature: opFeature,
      shadow_action,
    });
  };

  return (
    <section className="vd-block va-section">
      <h3 className="vd-block-title">Operational Actions</h3>
      <p className="tiny subtle">
        Vehicle-scoped CLI only. <strong>FLAG OFF</strong> is a normal administrative disable. <strong>Kill Switch</strong> is an emergency disable for a reported issue. Shadow does not change the active control version.
      </p>

      {disabled ? (
        <div className="rollout-banner warn">
          Actions are disabled until deploy/state exists for this vehicle (not missing).
        </div>
      ) : null}

      <div className="va-feature-row">
        <label className="va-field">
          <span>Feature</span>
          <select
            className="select"
            value={opFeature}
            onChange={(e) => setOpFeature(e.target.value)}
            disabled={disabled}
          >
            {FEATURES.map((f) => (
              <option key={f} value={f}>
                {f}
              </option>
            ))}
          </select>
        </label>
      </div>

      <div className="va-grid">
        <article className="va-card">
          <h4>Rollback</h4>
          <p className="tiny">
            Rollback metadata:{" "}
            <strong className={rbAvailable ? "vd-rb-ok" : "vd-rb-no"}>{rbAvailable ? "available" : "missing"}</strong>
          </p>
          <label className="va-check">
            <input type="checkbox" checked={syncRollback} onChange={(e) => setSyncRollback(e.target.checked)} disabled={disabled} />
            <span>
              <code className="mono">--sync-config</code> after successful rollback
            </span>
          </label>
          <button
            type="button"
            className="btn btn-primary va-btn"
            disabled={disabled || !rbAvailable || busy}
            onClick={doRollback}
          >
            {busy === "rollback" ? "Running fleet-rollback…" : `Rollback ${opFeature}`}
          </button>
        </article>

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
          <h4>Kill Switch (fleet-kill)</h4>
          <p className="tiny">
            Current kill_switch: <strong>{killOn ? "ON" : "OFF"}</strong>
          </p>
          <p className="tiny subtle">{KILL_POLICY[opFeature]}</p>
          <label className="va-field">
            <span>reason (required)</span>
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
              <code className="mono">--sync-config</code>
            </span>
          </label>
          <button type="button" className="btn btn-danger va-btn" disabled={disabled || busy} onClick={doKill}>
            {busy === "kill" ? "Running fleet-kill…" : "Apply Kill Switch"}
          </button>
        </article>

        <article className="va-card">
          <h4>Shadow mode (fleet-shadow)</h4>
          <p className="tiny">
            Current shadow_enabled: <strong>{shadowOn ? "ON" : "OFF"}</strong> — shadow compares against slot <strong>B</strong> /{" "}
            <span className="mono">{shadowBVersion}</span> from version_map.
          </p>
          <p className="tiny subtle">
            Shadow Mode does not change the active control version. B slot is recorded for comparison only. No{" "}
            <code className="mono">--sync-config</code> (not supported by CLI).
          </p>
          <div className="va-btn-row">
            <button type="button" className="btn va-btn" disabled={disabled || busy} onClick={() => doShadow("ON")}>
              {busy === "shadow" ? "Running…" : "Shadow ON"}
            </button>
            <button type="button" className="btn btn-secondary va-btn" disabled={disabled || busy} onClick={() => doShadow("OFF")}>
              Shadow OFF
            </button>
          </div>
        </article>
      </div>

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
