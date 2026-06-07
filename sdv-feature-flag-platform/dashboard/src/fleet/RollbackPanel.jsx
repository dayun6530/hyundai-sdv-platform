import { useEffect, useMemo, useState } from "react";
import {
  applyRollback,
  previewRollback,
  verifyRollback,
} from "../api/rollback.js";

const FEATURES = ["ADAS", "Navigation", "Energy"];
const FEATURE_LABELS = {
  ADAS: "ADAS",
  Navigation: "Navigation",
  Energy: "EV Energy",
};
const STACK_LIMIT = 3;

function normalizeStack(state, feature) {
  const stack = state?.features?.[feature]?.rollback_stack;
  if (!Array.isArray(stack)) return [];
  return stack.slice(0, STACK_LIMIT).map((entry) => ({
    version: String(entry?.version || "").trim(),
    image: String(entry?.docker_image || entry?.image || "").trim(),
    timestamp: String(entry?.timestamp || entry?.recorded_at || "").trim(),
    replaced_by: String(entry?.replaced_by || entry?.promoted_to || "").trim(),
  }));
}

function currentPrimary(state, feature) {
  const f = state?.features?.[feature];
  return String(f?.primary_version || f?.active_version || "—").trim() || "—";
}

export default function RollbackPanel({
  carId,
  state,
  disabled,
  syncRollback,
  onFleetRefresh,
  onRollbackMetaInvalidate,
}) {
  const [targets, setTargets] = useState({ ADAS: "", Navigation: "", Energy: "" });
  const [busy, setBusy] = useState(null);
  const [err, setErr] = useState("");
  const [successMsg, setSuccessMsg] = useState("");
  const [preview, setPreview] = useState(null);
  const [verifyResult, setVerifyResult] = useState(null);
  const [lastRestored, setLastRestored] = useState(null);

  const stacks = useMemo(() => {
    const out = {};
    for (const f of FEATURES) out[f] = normalizeStack(state, f);
    return out;
  }, [state]);

  useEffect(() => {
    setTargets((prev) => {
      const next = { ...prev };
      for (const feature of FEATURES) {
        const list = stacks[feature] || [];
        if (!list.length) next[feature] = "";
        else if (!list.some((e) => e.version === prev[feature])) {
          next[feature] = list[0].version;
        }
      }
      return next;
    });
    setPreview(null);
    setVerifyResult(null);
    setSuccessMsg("");
  }, [carId, stacks]);

  const runPreview = async (feature) => {
    if (disabled || busy) return;
    const target = targets[feature] || stacks[feature]?.[0]?.version;
    setBusy(`preview:${feature}`);
    setErr("");
    try {
      const data = await previewRollback({
        car_id: carId,
        feature,
        target_version: target,
      });
      setPreview({ feature, data });
    } catch (e) {
      setPreview(null);
      setErr(e.message || "Preview failed");
    } finally {
      setBusy(null);
    }
  };

  const runApply = async (feature) => {
    if (disabled || busy) return;
    const list = stacks[feature] || [];
    const targetVersion = targets[feature] || list[0]?.version;
    if (!targetVersion) return;
    const primary = currentPrimary(state, feature);
    const ok = window.confirm(
      `Apply rollback for ${carId} ${feature}?\nPrimary ${primary} → ${targetVersion}`,
    );
    if (!ok) return;
    setBusy(`apply:${feature}`);
    setErr("");
    setSuccessMsg("");
    setVerifyResult(null);
    try {
      const data = await applyRollback({
        car_id: carId,
        feature,
        target_version: targetVersion,
        sync_config: syncRollback,
      });
      if (!data.ok && data.result !== "no_op") {
        setErr(data.reason || data.error || "Rollback failed");
        setLastRestored(null);
      } else if (data.result === "no_op") {
        setSuccessMsg(data.warning || "No change (already at target version).");
      } else {
        setSuccessMsg(
          `Rollback success: ${feature} Primary restored from ${data.previous_primary_before_rollback} to ${data.restored_version}`,
        );
        setLastRestored({
          feature,
          version: data.restored_version,
        });
      }
      await onFleetRefresh?.();
      onRollbackMetaInvalidate?.();
    } catch (e) {
      setErr(e.message || "Apply rollback failed");
    } finally {
      setBusy(null);
    }
  };

  const runVerify = async (feature) => {
    if (disabled || busy) return;
    const expected =
      lastRestored?.feature === feature
        ? lastRestored.version
        : targets[feature] || stacks[feature]?.[0]?.version;
    setBusy(`verify:${feature}`);
    setErr("");
    try {
      const data = await verifyRollback({
        car_id: carId,
        feature,
        expected_primary: expected,
      });
      setVerifyResult({ feature, data });
      if (!data.verified) {
        setErr(
          `Verify mismatch: expected ${data.expected_primary}, actual ${data.actual_primary}`,
        );
      }
    } catch (e) {
      setVerifyResult(null);
      setErr(e.message || "Verify failed");
    } finally {
      setBusy(null);
    }
  };

  return (
    <article className="va-card va-card-rollback">
      <h4>Rollback</h4>
      <p className="tiny subtle">
        Restore Primary from <code className="mono">rollback_stack</code>. 최근{" "}
        {STACK_LIMIT}개 rollback target 보관 (Stack Limit: {STACK_LIMIT}).
      </p>
      <p className="tiny subtle va-rollback-hint">
        Rollback target = stack 최신 항목(또는 선택한 버전). Apply 후 Fleet Dashboard Primary가 즉시 갱신됩니다.
      </p>

      <div className="va-rollback-rows">
        {FEATURES.map((feature) => {
          const list = stacks[feature] || [];
          const primary = currentPrimary(state, feature);
          const target = targets[feature] || list[0]?.version || "";
          const canRollback = Boolean(list.length && target);
          return (
            <div key={feature} className="va-rollback-block">
              <div className="va-rollback-row">
                <div className="va-rollback-feature">
                  <strong>{FEATURE_LABELS[feature]}</strong>
                  <span className="tiny subtle">
                    Current Primary: <span className="mono">{primary}</span>
                  </span>
                  <span className="tiny subtle">
                    Rollback Target:{" "}
                    <span className="mono">{target || "—"}</span>
                  </span>
                </div>
                <select
                  className="select va-rollback-select"
                  value={targets[feature]}
                  onChange={(e) =>
                    setTargets((prev) => ({
                      ...prev,
                      [feature]: e.target.value,
                    }))
                  }
                  disabled={disabled || !list.length}
                >
                  {!list.length ? (
                    <option value="">No rollback target</option>
                  ) : (
                    list.map((entry) => (
                      <option key={entry.version} value={entry.version}>
                        {entry.version}
                      </option>
                    ))
                  )}
                </select>
                <div className="va-rollback-btn-group">
                  <button
                    type="button"
                    className="btn btn-secondary va-rollback-btn-sm"
                    disabled={disabled || !canRollback || Boolean(busy)}
                    onClick={() => runPreview(feature)}
                  >
                    {busy === `preview:${feature}` ? "…" : "Preview"}
                  </button>
                  <button
                    type="button"
                    className="btn btn-primary va-rollback-btn"
                    disabled={disabled || !canRollback || Boolean(busy)}
                    onClick={() => runApply(feature)}
                  >
                    {busy === `apply:${feature}` ? "Applying…" : "Apply Rollback"}
                  </button>
                  <button
                    type="button"
                    className="btn btn-secondary va-rollback-btn-sm"
                    disabled={disabled || Boolean(busy)}
                    onClick={() => runVerify(feature)}
                  >
                    {busy === `verify:${feature}` ? "…" : "Verify"}
                  </button>
                </div>
              </div>
              {list.length ? (
                <div className="va-rollback-stack">
                  <div className="label">Rollback Stack ({list.length}/{STACK_LIMIT})</div>
                  <ul className="va-rollback-stack-list">
                    {list.map((entry) => (
                      <li key={entry.version} className="mono tiny">
                        <strong>{entry.version}</strong>
                        {entry.image ? ` · ${entry.image}` : ""}
                        {entry.timestamp ? ` · ${entry.timestamp}` : ""}
                        {entry.replaced_by ? ` · replaced by ${entry.replaced_by}` : ""}
                      </li>
                    ))}
                  </ul>
                </div>
              ) : (
                <p className="tiny subtle va-rollback-stack-empty">
                  Stack empty — promote once to record a rollback target.
                </p>
              )}
            </div>
          );
        })}
      </div>

      {preview ? (
        <div className="va-rollback-preview tiny">
          <strong>Preview ({preview.feature})</strong>: {preview.data?.message || "—"}
          {preview.data?.would_change_primary === false ? " (no-op)" : ""}
        </div>
      ) : null}

      {successMsg ? (
        <div className="rollout-banner ok va-rollback-success">{successMsg}</div>
      ) : null}

      {verifyResult?.data ? (
        <div className="va-rollback-verify tiny">
          <strong>Verify ({verifyResult.feature})</strong>:{" "}
          {verifyResult.data.verified ? (
            <span className="vd-rb-ok">
              OK — expected {verifyResult.data.expected_primary}, actual{" "}
              {verifyResult.data.actual_primary}
              {verifyResult.data.history_found ? " · history found" : ""}
            </span>
          ) : (
            <span className="vd-rb-no">mismatch</span>
          )}
        </div>
      ) : null}

      {err ? <div className="error-banner va-err">{err}</div> : null}
    </article>
  );
}
