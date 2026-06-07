import { useEffect, useMemo, useState } from "react";
import { fetchRollbackStatus } from "../api/fleet.js";
import {
  featureShadowRolloutActive,
  getEffectiveVehicleStatus,
  getOtaSafetyChecklist,
} from "./fleetDashboardModel.js";
import VehicleActionsSection from "./VehicleActionsSection.jsx";
import { filterHistoryForCar, historyExtraCell } from "./vehicleHistoryFilter.js";

const FEATURE_ORDER = ["ADAS", "Navigation", "Energy"];

function kv(label, value) {
  return (
    <div className="vd-kv">
      <span className="vd-k">{label}</span>
      <span className="vd-v mono">{value ?? "—"}</span>
    </div>
  );
}

function FeatureDetailCard({ name, feat }) {
  if (!feat || typeof feat !== "object") {
    return (
      <article className="vd-feature-card vd-muted">
        <h4>{name}</h4>
        <p className="tiny">No feature state in deploy/state for this vehicle.</p>
      </article>
    );
  }

  const flagOn = (feat.flag || "").toUpperCase() === "ON";
  const kill = Boolean(feat.kill_switch);
  const shadow = Boolean(feat.shadow_enabled);
  const rollout = featureShadowRolloutActive(feat);
  const primarySlot = feat.primary_slot || feat.active_slot || "—";
  const primaryVer = feat.primary_version || feat.active_version || "—";
  const shadowSlot = feat.shadow_slot || "—";
  const shadowVer = feat.shadow_version || "—";

  return (
    <article className="vd-feature-card">
      <div className="vd-feature-head">
        <h4>{name}</h4>
        <div className="vd-badge-row">
          <span className={`vd-mini-badge ${flagOn ? "ok" : "warn"}`}>FLAG {flagOn ? "ON" : "OFF"}</span>
          <span className={`vd-mini-badge ${kill ? "danger" : "ok"}`}>KILL {kill ? "ON" : "OFF"}</span>
          <span className={`vd-mini-badge ${rollout ? "info" : shadow ? "info" : "ok"}`}>
            SHADOW {rollout ? "ROLLOUT" : shadow ? "ADMIN" : "OFF"}
          </span>
          <span className="vd-mini-badge neutral">PRIMARY {primaryVer} (slot {primarySlot})</span>
          <span className="vd-mini-badge neutral">
            SHADOW {shadow ? `${shadowVer} (slot ${shadowSlot})` : "OFF"}
          </span>
          {(feat.fallback_slot || feat.fallback_version) && (
            <span className="vd-mini-badge neutral">
              FALLBACK {feat.fallback_slot || "—"} / {feat.fallback_version || "—"}
            </span>
          )}
        </div>
      </div>
      <div className="vd-feature-grid">
        {kv("primary_slot", feat.primary_slot ?? "—")}
        {kv("primary_version", feat.primary_version ?? "—")}
        {kv("primary_image", feat.primary_image ?? "—")}
        {kv("shadow_image", feat.shadow_image ?? "—")}
        {kv("active_slot", feat.active_slot)}
        {kv("active_version", feat.active_version)}
        {kv("flag", feat.flag)}
        {kv("kill_switch", String(Boolean(feat.kill_switch)))}
        {kv("kill_reason", feat.kill_reason || "—")}
        {kv("shadow_enabled", String(Boolean(feat.shadow_enabled)))}
        {kv("shadow_slot", feat.shadow_slot)}
        {kv("shadow_version", feat.shadow_version)}
        {kv("fallback_slot", feat.fallback_slot)}
        {kv("fallback_version", feat.fallback_version)}
      </div>
      {name === "Navigation" ? (
        <div className="vd-feature-extra">
          {kv("realtime_route_flag", feat.realtime_route_flag === undefined ? "—" : String(feat.realtime_route_flag))}
          {kv("charging_recommend_flag", feat.charging_recommend_flag === undefined ? "—" : String(feat.charging_recommend_flag))}
        </div>
      ) : null}
      {name === "Energy" ? (
        <div className="vd-feature-extra">
          {kv("advanced_range_flag", feat.advanced_range_flag === undefined ? "—" : String(feat.advanced_range_flag))}
          {kv("charging_recommend_flag", feat.charging_recommend_flag === undefined ? "—" : String(feat.charging_recommend_flag))}
        </div>
      ) : null}
    </article>
  );
}

export default function VehicleDetailPanel({
  carId,
  vehicles,
  stateByCar,
  historyRecords,
  versionMap,
  onFleetRefresh,
  onClear,
}) {
  const [rollback, setRollback] = useState(null);
  const [rbErr, setRbErr] = useState("");
  const [rollbackReloadNonce, setRollbackReloadNonce] = useState(0);

  useEffect(() => {
    if (!carId) return;
    let cancelled = false;
    setRbErr("");
    setRollback(null);
    fetchRollbackStatus(carId)
      .then((data) => {
        if (!cancelled) setRollback(data);
      })
      .catch((e) => {
        if (!cancelled) setRbErr(e.message || "rollback-status failed");
      });
    return () => {
      cancelled = true;
    };
  }, [carId, rollbackReloadNonce]);

  const vehicle = useMemo(() => (vehicles || []).find((v) => v.car_id === carId), [vehicles, carId]);
  const state = carId ? stateByCar[carId] : null;
  const missing = !state || state.missing;
  const ota = missing ? {} : state.ota || {};
  const checklist = getOtaSafetyChecklist(missing ? null : state);
  const historyRows = useMemo(() => filterHistoryForCar(historyRecords, carId, 40), [historyRecords, carId]);

  const killFlagShadowLine = useMemo(() => {
    if (missing) return "—";
    const bits = [];
    for (const k of FEATURE_ORDER) {
      const f = state.features?.[k];
      if (!f) continue;
      bits.push(
        `${k}: flag=${f.flag || "?"} kill=${f.kill_switch ? "ON" : "OFF"} shadow=${featureShadowRolloutActive(f) ? "ROLLOUT" : f.shadow_enabled ? "ADMIN" : "OFF"}`,
      );
    }
    return bits.length ? bits.join(" · ") : "—";
  }, [missing, state]);

  if (!carId) {
    return (
      <section className="card vehicle-detail-panel vehicle-detail-empty">
        <p className="subtle">
          Select a vehicle from the Fleet Dashboard to inspect feature state and OTA safety conditions.
        </p>
      </section>
    );
  }

  return (
    <section className="card vehicle-detail-panel">
      <div className="card-header vehicle-detail-header">
        <div>
          <h2>Vehicle Detail: {carId}</h2>
          <p className="tiny subtle">
            Inspect OTA safety, rollback files, and history. Use <strong>Operational Actions</strong> below for vehicle-scoped CLI (rollback / flag / kill / shadow).
          </p>
        </div>
        {onClear ? (
          <button type="button" className="btn btn-secondary vd-clear" onClick={onClear}>
            Clear selection
          </button>
        ) : null}
      </div>

      {missing ? (
        <div className="error-banner vd-missing-banner">
          <strong>STATE MISSING</strong> — no <code className="mono">deploy/state/{carId}.json</code> (or empty placeholder). Detail fields below are limited.
        </div>
      ) : null}

      <div className={`vd-approval-banner ${ota.driver_approved ? "approved" : "waiting"}`}>
        <span className="vd-approval-label">Driver Approval</span>
        <span className="vd-approval-value">{ota.driver_approved ? "Approved" : "Waiting"}</span>
      </div>

      <section className="vd-block">
        <h3 className="vd-block-title">Rollout readiness (OTA safety gate)</h3>
        <p className="tiny subtle">
          Same 10-condition gate as the fleet table. This is informational — <code className="mono">fleet-deploy</code> still evaluates per vehicle on the server.
        </p>
        <div className="vd-readiness-row">
          <span className="subtle">Rollout Readiness:</span>
          <span
            className={`ota-safety ${
              checklist.rolloutReadiness === "READY"
                ? "ota-ready"
                : checklist.rolloutReadiness === "WAITING APPROVAL"
                  ? "ota-wait"
                  : checklist.rolloutReadiness === "MISSING"
                    ? "ota-missing"
                    : "ota-blocked"
            }`}
          >
            {checklist.rolloutReadiness}
          </span>
        </div>
      </section>

      <section className="vd-block">
        <h3 className="vd-block-title">Kill / Flag / Shadow (summary)</h3>
        <p className="mono tiny vd-mono-block">{killFlagShadowLine}</p>
      </section>

      <section className="vd-block">
        <h3 className="vd-block-title">Vehicle basics</h3>
        <div className="vd-basics-grid">
          {kv("car_id", carId)}
          {kv("model", vehicle?.model)}
          {kv("region", vehicle?.region)}
          {kv("group", vehicle?.group)}
          {kv("status", missing ? "missing" : getEffectiveVehicleStatus(state, vehicle))}
          {!missing && state.runtime?.source ? (
            <div className="vd-kv">
              <span className="vd-k">runtime</span>
              <span className="vd-v mono">
                {String(state.runtime.car_process_running)} ({state.runtime.source}
                {state.runtime.probed_at ? ` @ ${state.runtime.probed_at}` : ""})
              </span>
            </div>
          ) : null}
          {kv("network", ota.network)}
          {kv("driver_approved", missing ? "—" : String(Boolean(ota.driver_approved)))}
          {kv("download_status", ota.download_status)}
          {kv("install_status", ota.install_status)}
        </div>
      </section>

      <section className="vd-block">
        <h3 className="vd-block-title">OTA Safety Checklist</h3>
        <div className="vd-checklist-head">
          <span className="subtle">Overall:</span>
          <span
            className={`ota-safety ${
              checklist.level === "READY"
                ? "ota-ready"
                : checklist.level === "WAITING_APPROVAL"
                  ? "ota-wait"
                  : checklist.level === "MISSING"
                    ? "ota-missing"
                    : "ota-blocked"
            }`}
          >
            {checklist.level}
          </span>
        </div>
        {checklist.items.length ? (
          <ul className="vd-checklist">
            {checklist.items.map((it) => (
              <li key={it.id} className={it.pass ? "pass" : "fail"}>
                <span className="vd-check-icon" aria-hidden>
                  {it.pass ? "✅" : "❌"}
                </span>
                <span>
                  <strong>{it.label}</strong>
                  <span className="vd-check-detail"> — {it.detail}</span>
                </span>
              </li>
            ))}
          </ul>
        ) : (
          <p className="tiny subtle">No checklist items (missing state).</p>
        )}
      </section>

      <section className="vd-block">
        <h3 className="vd-block-title">Rollback snapshot files</h3>
        {rbErr ? <div className="rollout-banner warn">{rbErr}</div> : null}
        <p className="tiny subtle">
          Presence of <code className="mono">deploy/rollback/&lt;car_id&gt;/&lt;Feature&gt;.json</code> (file exists only).
        </p>
        <div className="vd-rollback-grid">
          {FEATURE_ORDER.map((f) => (
            <div key={f} className="vd-rollback-cell">
              <span className="mono">{f}</span>
              <span className={rollback && rollback[f] ? "vd-rb-ok" : "vd-rb-no"}>
                {rollback ? (rollback[f] ? "available" : "missing") : "…"}
              </span>
            </div>
          ))}
        </div>
      </section>

      <section className="vd-block">
        <h3 className="vd-block-title">Feature detail</h3>
        <div className="vd-feature-deck">
          {FEATURE_ORDER.map((f) => (
            <FeatureDetailCard key={f} name={f} feat={missing ? null : state.features?.[f]} />
          ))}
        </div>
      </section>

      <VehicleActionsSection
        carId={carId}
        missing={missing}
        state={state}
        onFleetRefresh={onFleetRefresh}
        onRollbackMetaInvalidate={() => setRollbackReloadNonce((n) => n + 1)}
      />

      <section className="vd-block">
        <h3 className="vd-block-title">Recent fleet history (this vehicle)</h3>
        <p className="tiny subtle">Filtered from parsed <code className="mono">fleet_history.jsonl</code> (includes <code className="mono">results[].car_id</code> matches).</p>
        {!historyRows.length ? (
          <p className="empty-state">No matching history lines for {carId}.</p>
        ) : (
          <div className="table-wrap vd-history-wrap">
            <table className="vd-history-table">
              <thead>
                <tr>
                  <th>timestamp</th>
                  <th>action</th>
                  <th>feature</th>
                  <th>result</th>
                  <th>reason</th>
                  <th>summary / targets</th>
                </tr>
              </thead>
              <tbody>
                {historyRows.map((rec, idx) => (
                  <tr key={`${rec.timestamp}-${idx}`}>
                    <td className="mono tiny">{rec.timestamp || "—"}</td>
                    <td className="mono tiny">{rec.action || "—"}</td>
                    <td>{rec.feature || "—"}</td>
                    <td>{rec.result || "—"}</td>
                    <td className="tiny">{rec.reason || "—"}</td>
                    <td className="tiny mono">
                      {rec.summary || "—"}
                      <div className="vd-history-extra">{historyExtraCell(rec)}</div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>
    </section>
  );
}
