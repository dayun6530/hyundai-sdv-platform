import { useMemo, useState } from "react";
import {
  computeFleetSummaryCards,
  computeOtaSafetyFleet,
  featureShadowRolloutActive,
  filterFleetRows,
  getEffectiveVehicleStatus,
} from "./fleetDashboardModel.js";

const FEATURE_KEYS = ["ADAS", "Navigation", "Energy"];

function classNames(...xs) {
  return xs.filter(Boolean).join(" ");
}

function FeatureStatusBadge({ name, feat }) {
  if (!feat || typeof feat !== "object") {
    return (
      <div className="feature-badge fb-muted">
        <div className="fb-title">{name}</div>
        <div className="fb-line">—</div>
      </div>
    );
  }

  const flag = (feat.flag || "").toUpperCase();
  const kill = Boolean(feat.kill_switch);
  const shadow = Boolean(feat.shadow_enabled);
  const rollout = featureShadowRolloutActive(feat);
  const primaryVer = feat.primary_version || feat.active_version || "—";
  const primaryImg = feat.primary_image || "";

  const shadowVer = feat.shadow_version || "—";
  const shadowImg = feat.shadow_image || "";

  const lines = [];
  lines.push({
    text: `PRIMARY → ${primaryVer}${primaryImg ? ` (${primaryImg})` : ""}`,
    cls: "fb-line",
  });
  lines.push({
    text: shadow ? `SHADOW  → ${shadowVer}${shadowImg ? ` (${shadowImg})` : ""}` : "SHADOW  → OFF",
    cls: classNames("fb-line", shadow && "fb-info"),
  });
  lines.push({
    text: `FLAG ${flag === "ON" ? "ON" : "OFF"}`,
    cls: classNames("fb-line", flag !== "ON" && "fb-warn"),
  });
  lines.push({
    text: kill ? "KILL ON" : "KILL OFF",
    cls: classNames("fb-line", kill && "fb-danger"),
  });
  if (rollout) {
    lines.push({ text: "SHADOW rollout ACTIVE (Primary ≠ Shadow)", cls: classNames("fb-line", "fb-info") });
  }

  if (name === "Navigation") {
    const rr = feat.realtime_route_flag;
    const cr = feat.charging_recommend_flag;
    if (rr !== undefined) {
      lines.push({
        text: `Realtime ${rr ? "ON" : "OFF"}`,
        cls: classNames("fb-line", !rr && "fb-muted"),
      });
    }
    if (cr !== undefined) {
      lines.push({
        text: `Charging ${cr ? "ON" : "OFF"}`,
        cls: classNames("fb-line", !cr && "fb-muted"),
      });
    }
  }
  if (name === "Energy") {
    const ar = feat.advanced_range_flag;
    const cr = feat.charging_recommend_flag;
    if (ar !== undefined) {
      lines.push({
        text: `Advanced ${ar ? "ON" : "OFF"}`,
        cls: classNames("fb-line", !ar && "fb-muted"),
      });
    }
    if (cr !== undefined) {
      lines.push({
        text: `Charging ${cr ? "ON" : "OFF"}`,
        cls: classNames("fb-line", !cr && "fb-muted"),
      });
    }
  }

  return (
    <div className="feature-badge">
      <div className="fb-title">{name}</div>
      {lines.map((l, i) => (
        <div key={i} className={l.cls}>
          {l.text}
        </div>
      ))}
    </div>
  );
}

function OtaSafetyBadge({ state }) {
  const r = computeOtaSafetyFleet(state);
  const cls =
    r.level === "READY"
      ? "ota-safety ota-ready"
      : r.level === "WAITING_APPROVAL"
        ? "ota-safety ota-wait"
        : r.level === "MISSING"
          ? "ota-safety ota-missing"
          : "ota-safety ota-blocked";
  return (
    <span className={cls} title={r.tooltip || undefined}>
      {r.level}
    </span>
  );
}

function FleetSummaryCards({ metrics }) {
  const items = [
    { label: "Total Vehicles", value: metrics.totalVehicles },
    { label: "Online Vehicles", value: metrics.onlineVehicles },
    { label: "Canary Vehicles", value: metrics.canaryVehicles },
    { label: "Beta Vehicles", value: metrics.betaVehicles },
    { label: "Stable Vehicles", value: metrics.stableVehicles },
    { label: "Kill Switch Active", value: metrics.killSwitchActive },
    { label: "Shadow Mode Active", value: metrics.shadowModeActive },
    { label: "Waiting Approval", value: metrics.waitingApproval },
    { label: "Install Completed", value: metrics.installCompleted },
    { label: "Safety Ready", value: metrics.safetyReady },
  ];
  return (
    <div className="fleet-cards-grid" aria-label="Fleet summary">
      {items.map((it) => (
        <div key={it.label} className="fleet-card">
          <div className="fleet-card-label">{it.label}</div>
          <div className="fleet-card-value">{it.value}</div>
        </div>
      ))}
    </div>
  );
}

function FleetFilters({ filters, setFilters }) {
  const row = (key, label, options) => (
    <label className="fleet-filter-field">
      <span>{label}</span>
      <select
        value={filters[key]}
        onChange={(e) => setFilters((f) => ({ ...f, [key]: e.target.value }))}
      >
        {options.map((o) => (
          <option key={o} value={o}>
            {o}
          </option>
        ))}
      </select>
    </label>
  );

  return (
    <div className="fleet-dash-filters" aria-label="Fleet filters">
      {row("group", "Group", ["all", "canary", "beta", "stable"])}
      {row("region", "Region", ["all", "KR", "US", "EU"])}
      {row("model", "Model", ["all", "IONIQ5", "IONIQ6", "KONA_EV"])}
      {row("status", "Status", ["all", "online", "offline", "missing"])}
      {row("featureIssue", "Feature issue", ["all", "kill_active", "shadow_active", "flag_off"])}
    </div>
  );
}

export default function FleetDashboard({
  vehicles,
  stateByCar,
  loading,
  onToggleApproval,
  selectedCarId,
  onSelectCarId,
}) {
  const [filters, setFilters] = useState({
    group: "all",
    region: "all",
    model: "all",
    status: "all",
    featureIssue: "all",
  });

  const metrics = useMemo(
    () => computeFleetSummaryCards(vehicles, stateByCar),
    [vehicles, stateByCar],
  );

  const rows = useMemo(
    () => filterFleetRows(vehicles, stateByCar, filters),
    [vehicles, stateByCar, filters],
  );

  return (
    <section className="card fleet-dashboard-section">
      <div className="card-header">
        <div>
          <h2>Fleet Dashboard</h2>
          {selectedCarId ? (
            <p className="fleet-selected-pill subtle">
              Selected: <strong className="mono">{selectedCarId}</strong>
            </p>
          ) : null}
        </div>
        <span className="subtle">polling `GET /api/fleet/*`</span>
      </div>
      {loading ? <p className="fleet-dash-loading">Fleet data loading…</p> : null}
      <FleetSummaryCards metrics={metrics} />
      <FleetFilters filters={filters} setFilters={setFilters} />
      <div className="fleet-dash-table-wrap">
        <table className="fleet-dash-table">
          <thead>
            <tr>
              <th>car_id / actions</th>
              <th>model</th>
              <th>region</th>
              <th>group</th>
              <th>status</th>
              <th>ADAS</th>
              <th>Navigation</th>
              <th>Energy</th>
              <th>OTA Safety</th>
              <th>Approval</th>
              <th>Last Install Status</th>
            </tr>
          </thead>
          <tbody>
            {!rows.length ? (
              <tr>
                <td colSpan={11} className="empty-state">
                  No vehicles match the current filters.
                </td>
              </tr>
            ) : null}
            {rows.map((v) => {
              const st = stateByCar[v.car_id];
              const missing = !st || Boolean(st?.missing);
              const effStatus = missing ? "missing" : getEffectiveVehicleStatus(st, v);
              const ota = missing ? {} : st?.ota || {};
              const feats = st?.features || {};
              const safety = computeOtaSafetyFleet(missing ? null : st);
              return (
                <tr
                  key={v.car_id}
                  className={selectedCarId === v.car_id ? "fleet-dash-row fleet-dash-row-selected" : "fleet-dash-row"}
                  onClick={() => onSelectCarId?.(v.car_id)}
                >
                  <td onClick={(e) => e.stopPropagation()}>
                    <strong>{v.car_id}</strong>
                    {missing ? (
                      <div className="fleet-missing-hint">STATE MISSING</div>
                    ) : null}
                    <div className="fleet-row-actions">
                      <button
                        type="button"
                        className="btn btn-secondary fleet-view-details"
                        onClick={() => onSelectCarId?.(v.car_id)}
                      >
                        View Details
                      </button>
                    </div>
                  </td>
                  <td>{v.model}</td>
                  <td>{v.region}</td>
                  <td>{v.group}</td>
                  <td>
                    <div>{effStatus}</div>
                    {!missing ? (
                      <div className="fleet-net-hint">
                        network: <span className="mono">{String(ota.network ?? "—")}</span>
                      </div>
                    ) : null}
                  </td>
                  <td>
                    <FeatureStatusBadge name="ADAS" feat={missing ? null : feats.ADAS} />
                  </td>
                  <td>
                    <FeatureStatusBadge name="Navigation" feat={missing ? null : feats.Navigation} />
                  </td>
                  <td>
                    <FeatureStatusBadge name="Energy" feat={missing ? null : feats.Energy} />
                  </td>
                  <td>
                    <OtaSafetyBadge state={missing ? null : st} />
                    {!missing && safety.level !== "READY" ? (
                      <div className="ota-safety-hint" title={safety.tooltip}>
                        {safety.tooltip.slice(0, 80) + (safety.tooltip.length > 80 ? "…" : "")}
                      </div>
                    ) : null}
                  </td>
                  <td onClick={(e) => e.stopPropagation()}>
                    {missing ? (
                      "—"
                    ) : ota.driver_approved ? (
                      <span className="approval-ok">Approved</span>
                    ) : (
                      <div className="approval-wait">
                        <div>Waiting Driver Approval</div>
                        <button type="button" className="btn" onClick={() => onToggleApproval(v.car_id, true)}>
                          Approve
                        </button>
                      </div>
                    )}
                    {!missing && ota.driver_approved ? (
                      <button type="button" className="btn btn-danger" onClick={() => onToggleApproval(v.car_id, false)}>
                        Unapprove
                      </button>
                    ) : null}
                  </td>
                  <td className="install-col" onClick={(e) => e.stopPropagation()}>
                    {missing ? (
                      "—"
                    ) : (
                      <>
                        <div>
                          download: <code>{String(ota.download_status || "—")}</code>
                        </div>
                        <div>
                          install: <code>{String(ota.install_status || "—")}</code>
                        </div>
                      </>
                    )}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </section>
  );
}
