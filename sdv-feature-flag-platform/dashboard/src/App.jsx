import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  fetchFleetHistory,
  fetchFleetStates,
  fetchFleetVehicles,
  fetchPackageRegistry,
  fetchVersionMap,
  fleetDeploy,
  parseSummaryJsonFromStdout,
  setDriverApproval,
} from "./api/fleet";
import {
  filterCanaryVehicles,
  loadFleetHistoryNormalized,
  loadPackageRegistryNormalized,
  loadVehicleStatesNormalized,
  loadVersionMapNormalized,
  loadVehiclesNormalized,
  mergeWarnings,
} from "./fleet/fleetData";
import PackageRegistryTable from "./fleet/PackageRegistryTable.jsx";
import PackageUploadPanel from "./fleet/PackageUploadPanel.jsx";
import FleetDashboard from "./fleet/FleetDashboard.jsx";
import FleetDeployPanel from "./fleet/FleetDeployPanel.jsx";
import VehicleDetailPanel from "./fleet/VehicleDetailPanel.jsx";

function formatDate(value) {
  if (!value) {
    return "-";
  }
  return value.replace("T", " ");
}

function WarningBanner({ warnings }) {
  if (!warnings?.length) {
    return null;
  }

  return (
    <section className="warning-banner">
      <div className="warning-title">Warnings ({warnings.length})</div>
      <ul className="warning-list">
        {warnings.map((warning) => (
          <li key={warning}>{warning}</li>
        ))}
      </ul>
    </section>
  );
}

function LoadingBanner({ loading }) {
  if (!loading) {
    return null;
  }

  return (
    <section className="loading-banner">
      Loading fleet data (vehicles, state, version map)…
    </section>
  );
}

function Header({ warningCount, connected, lastUpdated, updating }) {
  return (
    <header className="page-header">
      <div>
        <h1>SDV Deployment / Release Dashboard</h1>
        <p>Fleet-focused dashboard: car_1–car_10 state, version map, packages, deploy, and command history.</p>
        <div className="header-meta">
          <span>Last refreshed: <span className="mono">{formatDate(lastUpdated)}</span></span>
          {updating ? <span className="updating-text">updating...</span> : null}
        </div>
      </div>
      <div className="header-badges">
        <span className={`pill ${connected ? "pill-success" : "pill-error"}`}>
          {connected ? "status-server connected" : "status-server disconnected"}
        </span>
        <span className={`pill ${warningCount > 0 ? "pill-warning" : "pill-neutral"}`}>
          warnings: {warningCount}
        </span>
      </div>
    </header>
  );
}

function VersionRegistryTable({ rows }) {
  if (!rows?.length) {
    return (
      <section className="card">
        <div className="card-header">
          <div>
            <h2>Version Map (A / B)</h2>
            <p className="tiny subtle">Currently assigned slot versions — not the full package catalog.</p>
          </div>
        </div>
        <div className="empty-state">No version map data.</div>
      </section>
    );
  }

  return (
    <section className="card">
      <div className="card-header">
        <div>
          <h2>Version Map (A / B)</h2>
          <p className="tiny subtle">Currently assigned slot versions — separate from Package Registry.</p>
        </div>
        <span className="subtle">from `deploy/version_map.json`</span>
      </div>
      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Feature</th>
              <th>A version</th>
              <th>A role</th>
              <th>A binary</th>
              <th>B version</th>
              <th>B role</th>
              <th>B binary</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((r) => (
              <tr key={r.feature}>
                <td className="mono">{r.feature}</td>
                <td className="mono">{r.slotA?.version || "-"}</td>
                <td>{r.slotA?.role || "-"}</td>
                <td className="mono tiny">{r.slotA?.binary || "-"}</td>
                <td className="mono">{r.slotB?.version || "-"}</td>
                <td>{r.slotB?.role || "-"}</td>
                <td className="mono tiny">{r.slotB?.binary || "-"}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}

function FleetHistoryJsonlPanel({ records, meta }) {
  if (!records?.length) {
    return (
      <section className="card">
        <div className="card-header">
          <h2>Fleet command history</h2>
          <span className="subtle">`deploy/history/fleet_history.jsonl`</span>
        </div>
        <div className="empty-state">No fleet history lines yet.</div>
      </section>
    );
  }

  return (
    <section className="card">
      <div className="card-header">
        <h2>Fleet command history</h2>
        <span className="subtle">
          showing {meta.record_count} (parsed {meta.parsed_total ?? meta.record_count}
          {meta.skipped_lines ? `, skipped ${meta.skipped_lines}` : ""})
        </span>
      </div>
      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <th>timestamp</th>
              <th>action</th>
              <th>scope</th>
              <th>car / targets</th>
              <th>feature</th>
              <th>result</th>
              <th>reason</th>
            </tr>
          </thead>
          <tbody>
            {records.map((rec, idx) => (
              <tr key={`${rec.timestamp || ""}-${idx}`}>
                <td className="mono tiny">{rec.timestamp || "-"}</td>
                <td className="mono tiny">{rec.action || "-"}</td>
                <td>{rec.scope || "-"}</td>
                <td className="tiny mono">
                  {rec.car_id ||
                    (Array.isArray(rec.targets) ? rec.targets.join(",") : "") ||
                    (Array.isArray(rec.success_targets) ? `${rec.success_targets.length} targets` : "-")}
                </td>
                <td>{rec.feature || "-"}</td>
                <td>{rec.result || "-"}</td>
                <td className="tiny">{rec.reason || "-"}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}

function FleetDataDebugDetails({ vehicles, versionRows, states, historyMeta, canaryIds }) {
  return (
    <details className="card fleet-data-probe" style={{ marginTop: 12 }}>
      <summary className="card-header" style={{ cursor: "pointer" }}>
        <strong>Fleet data probe</strong> (read-only checks)
      </summary>
      <div className="tiny mono" style={{ padding: "0 16px 16px" }}>
        <div>vehicles count: {(vehicles || []).length} (expected 10)</div>
        <div>canary car_ids: {(canaryIds || []).join(", ") || "-"}</div>
        <div>version_map rows: {(versionRows || []).length}</div>
        <div>state objects: {(states || []).length}</div>
        <div>
          fleet_history records (tail): {historyMeta?.record_count ?? 0} / parsed_total:{" "}
          {historyMeta?.parsed_total ?? "-"}
        </div>
      </div>
    </details>
  );
}

function FleetSummary({ summary, rawStdout, rawStderr }) {
  if (!summary && !rawStdout && !rawStderr) {
    return (
      <section className="card">
        <div className="card-header">
          <h2>Deploy Summary</h2>
        </div>
        <div className="empty-state">No fleet deploy run yet.</div>
      </section>
    );
  }

  return (
    <section className="card">
      <div className="card-header">
        <h2>Deploy Summary</h2>
        {summary?.deploy_id ? <span className="mono subtle">{summary.deploy_id}</span> : null}
      </div>

      {summary ? (
        <>
          <div className="kv">
            <div className="k">timestamp</div>
            <div className="v mono">{summary.timestamp || "-"}</div>
            <div className="k">target</div>
            <div className="v mono">
              {summary.target_type} {summary.group ? `group=${summary.group}` : ""}{" "}
              {summary.region ? `region=${summary.region}` : ""}
            </div>
            <div className="k">feature</div>
            <div className="v mono">{summary.feature} slot={summary.target_slot} version={summary.target_version}</div>
            <div className="k">result</div>
            <div className="v">
              <span className={`status-badge ${summary.failed > 0 ? "status-red" : "status-green"}`}>
                total={summary.total} success={summary.success} failed={summary.failed}
              </span>
            </div>
          </div>

          <div style={{ marginTop: 14 }}>
            <div className="label">reason_counts</div>
            <div className="tiny mono">
              {summary.reason_counts && Object.keys(summary.reason_counts).length
                ? Object.entries(summary.reason_counts).map(([k, v]) => `${k}:${v}`).join("  ")
                : "none"}
            </div>
          </div>
        </>
      ) : null}

      {summary ? (
        <div style={{ marginTop: 14 }}>
          <div className="label">[SUMMARY_JSON]</div>
          <div className="json-box mono">{JSON.stringify(summary, null, 2)}</div>
        </div>
      ) : null}

      {rawStdout ? (
        <div style={{ marginTop: 14 }}>
          <div className="label">raw stdout</div>
          <div className="json-box mono">{rawStdout}</div>
        </div>
      ) : null}

      {rawStderr ? (
        <div style={{ marginTop: 14 }}>
          <div className="label">raw stderr</div>
          <div className="json-box mono">{rawStderr}</div>
        </div>
      ) : null}
    </section>
  );
}

export default function App() {
  const [lastUpdated, setLastUpdated] = useState("");
  const [fleetReachable, setFleetReachable] = useState(false);

  const [fleetVehicles, setFleetVehicles] = useState([]);
  const [fleetStates, setFleetStates] = useState([]);
  const [versionMap, setVersionMap] = useState(null);
  const [versionMapRows, setVersionMapRows] = useState([]);
  const [fleetHistoryRecords, setFleetHistoryRecords] = useState([]);
  const [fleetHistoryMeta, setFleetHistoryMeta] = useState({});
  const [packageRegistryPackages, setPackageRegistryPackages] = useState([]);
  const [fleetApiWarnings, setFleetApiWarnings] = useState([]);
  const [fleetError, setFleetError] = useState("");
  const [fleetDeploying, setFleetDeploying] = useState(false);
  const [fleetDeployError, setFleetDeployError] = useState("");
  const [fleetDeployStdout, setFleetDeployStdout] = useState("");
  const [fleetDeployStderr, setFleetDeployStderr] = useState("");
  const [fleetSummary, setFleetSummary] = useState(null);
  const [fleetBootLoading, setFleetBootLoading] = useState(true);
  const fleetFirstSuccess = useRef(false);
  const [selectedCarId, setSelectedCarId] = useState("");

  const reloadFleetData = useCallback(async () => {
    try {
      const [v, s, m, h] = await Promise.all([
        fetchFleetVehicles(),
        fetchFleetStates(),
        fetchVersionMap(),
        fetchFleetHistory(200),
      ]);
      let pr = { ok: true, packages: [], warnings: [], raw: null };
      try {
        pr = await fetchPackageRegistry();
      } catch {
        pr = { ok: false, packages: [], warnings: ["package registry API unavailable (update status-server)"], raw: null };
      }
      const vn = loadVehiclesNormalized(v);
      const sn = loadVehicleStatesNormalized(s);
      const mn = loadVersionMapNormalized(m);
      const hn = loadFleetHistoryNormalized(h);
      const prn = loadPackageRegistryNormalized(pr);
      setFleetVehicles(vn.vehicles);
      setFleetStates(sn.states);
      setVersionMap(m || null);
      setVersionMapRows(mn.rows);
      setFleetHistoryRecords(hn.records);
      setFleetHistoryMeta(hn.meta);
      setPackageRegistryPackages(prn.packages);
      const histSkip =
        hn.meta?.skipped_lines > 0
          ? [`fleet history: parse skipped lines: ${hn.meta.skipped_lines}`]
          : [];
      setFleetApiWarnings(mergeWarnings(vn.warnings, sn.warnings, mn.warnings, hn.warnings, prn.warnings, histSkip));
      setFleetError("");
      setFleetReachable(true);
      setLastUpdated(new Date().toISOString().slice(0, 19));
      if (!fleetFirstSuccess.current) {
        fleetFirstSuccess.current = true;
        setFleetBootLoading(false);
      }
    } catch (err) {
      setFleetReachable(false);
      setFleetError(err.message || "Failed to load fleet");
      setFleetBootLoading(false);
    }
  }, []);

  const refreshAllFleetAndStatus = useCallback(async () => {
    await reloadFleetData();
  }, [reloadFleetData]);

  useEffect(() => {
    reloadFleetData();
    const timer = window.setInterval(reloadFleetData, 2500);
    return () => window.clearInterval(timer);
  }, [reloadFleetData]);

  const stateByCar = useMemo(() => Object.fromEntries((fleetStates || []).map((s) => [s.car_id, s])), [fleetStates]);
  const canaryVehicleIds = useMemo(() => filterCanaryVehicles(fleetVehicles).map((v) => v.car_id), [fleetVehicles]);

  const onDeploy = async (payload) => {
    try {
      setFleetDeployError("");
      setFleetDeployStderr("");
      setFleetDeploying(true);
      const r = await fleetDeploy(payload);
      setFleetDeployStdout(r?.stdout || "");
      setFleetDeployStderr(r?.stderr || "");
      const summary = parseSummaryJsonFromStdout(r?.stdout || "");
      setFleetSummary(summary);
      await refreshAllFleetAndStatus();
    } catch (err) {
      setFleetDeployError(err.message || "Deploy failed");
    } finally {
      setFleetDeploying(false);
    }
  };

  const toggleApproval = async (car_id, approved) => {
    try {
      await setDriverApproval(car_id, approved);
      await refreshAllFleetAndStatus();
    } catch (err) {
      setFleetError(err.message || "Failed to update approval");
    }
  };

  return (
    <main className="app-shell">
      <Header
        warningCount={fleetApiWarnings.length}
        connected={fleetReachable}
        lastUpdated={lastUpdated}
        updating={false}
      />

      <LoadingBanner loading={fleetBootLoading && !fleetReachable} />

      <section className="card intro-card">
        <div className="legend-title">Fleet (car_1 ~ car_10)</div>
        <div className="legend-text">
          <span>
            <strong>Per vehicle</strong>: pick a car below — state comes from `deploy/state/car_*.json` (slots, flags,
            shadow, OTA).
          </span>
          <span>
            <strong>Fleet-wide</strong>: `deploy/version_map.json` A/B and package registry drive rollout targets; history
            is `deploy/history/fleet_history.jsonl`.
          </span>
          <span>
            <strong>API</strong>: `GET /api/fleet/*` via Vite proxy to status-server. Deploy runs `DeploymentMgr
            fleet-deploy …` (summary below).
          </span>
        </div>
      </section>

      {fleetError ? <section className="error-banner">Fleet API error: {fleetError}</section> : null}
      <WarningBanner warnings={fleetApiWarnings} />

      <FleetDashboard
        vehicles={fleetVehicles}
        stateByCar={stateByCar}
        loading={fleetBootLoading}
        onToggleApproval={toggleApproval}
        selectedCarId={selectedCarId}
        onSelectCarId={setSelectedCarId}
      />

      <VehicleDetailPanel
        carId={selectedCarId}
        vehicles={fleetVehicles}
        stateByCar={stateByCar}
        historyRecords={fleetHistoryRecords}
        versionMap={versionMap}
        onFleetRefresh={refreshAllFleetAndStatus}
        onClear={() => setSelectedCarId("")}
      />

      <PackageUploadPanel onRegistered={reloadFleetData} />

      <section className="two-col-grid fleet-registry-history">
        <VersionRegistryTable rows={versionMapRows} />
        <PackageRegistryTable
          packages={packageRegistryPackages}
          versionMapRaw={versionMap?.raw}
          onAssigned={reloadFleetData}
        />
      </section>

      <FleetHistoryJsonlPanel records={fleetHistoryRecords} meta={fleetHistoryMeta} />

      <FleetDataDebugDetails
        vehicles={fleetVehicles}
        versionRows={versionMapRows}
        states={fleetStates}
        historyMeta={fleetHistoryMeta}
        canaryIds={canaryVehicleIds}
      />

      <section className="fleet-grid">
        <FleetDeployPanel
          vehicles={fleetVehicles}
          versionMap={versionMap}
          stateByCar={stateByCar}
          onDeploy={onDeploy}
          deploying={fleetDeploying}
          deployError={fleetDeployError}
        />
        <FleetSummary summary={fleetSummary} rawStdout={fleetDeployStdout} rawStderr={fleetDeployStderr} />
      </section>
    </main>
  );
}
