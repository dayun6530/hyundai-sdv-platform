import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  fetchFleetHistory,
  fetchFleetStates,
  fetchFleetVehicles,
  fetchVersionMap,
  fleetDeploy,
  parseSummaryJsonFromStdout,
  setDriverApproval,
} from "./api/fleet";
import {
  filterCanaryVehicles,
  loadFleetHistoryNormalized,
  loadVehicleStatesNormalized,
  loadVersionMapNormalized,
  loadVehiclesNormalized,
  mergeWarnings,
} from "./fleet/fleetData";
import FleetDashboard from "./fleet/FleetDashboard.jsx";
import FleetDeployPanel from "./fleet/FleetDeployPanel.jsx";
import CampaignManagementPanel from "./fleet/CampaignManagementPanel.jsx";
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
        <p>
          Campaign-first OTA demo (Validate → Candidate → Promote). Fleet overview and optional legacy tools are under
          Advanced sections below.
        </p>
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
      const vn = loadVehiclesNormalized(v);
      const sn = loadVehicleStatesNormalized(s);
      const mn = loadVersionMapNormalized(m);
      const hn = loadFleetHistoryNormalized(h);
      setFleetVehicles(vn.vehicles);
      setFleetStates(sn.states);
      setVersionMap(m || null);
      setVersionMapRows(mn.rows);
      setFleetHistoryRecords(hn.records);
      setFleetHistoryMeta(hn.meta);
      const histSkip =
        hn.meta?.skipped_lines > 0
          ? [`fleet history: parse skipped lines: ${hn.meta.skipped_lines}`]
          : [];
      setFleetApiWarnings(mergeWarnings(vn.warnings, sn.warnings, mn.warnings, hn.warnings, histSkip));
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

      {fleetError ? <section className="error-banner">Fleet API error: {fleetError}</section> : null}
      <WarningBanner warnings={fleetApiWarnings} />

      <section className="card fleet-overview-section fleet-overview-section-first">
        <div className="card-header">
          <div>
            <h2>Fleet overview</h2>
            <p className="tiny subtle">
              Select a vehicle — detail appears directly below. Campaign demo uses the campaign target (e.g. car_1).
            </p>
          </div>
        </div>
        <div style={{ padding: "0 16px 8px" }}>
          <FleetDashboard
            vehicles={fleetVehicles}
            stateByCar={stateByCar}
            loading={fleetBootLoading}
            onToggleApproval={toggleApproval}
            selectedCarId={selectedCarId}
            onSelectCarId={setSelectedCarId}
          />
        </div>
        {selectedCarId ? (
          <div className="fleet-selected-detail-wrap" style={{ padding: "0 16px 16px" }}>
            <VehicleDetailPanel
              carId={selectedCarId}
              vehicles={fleetVehicles}
              stateByCar={stateByCar}
              historyRecords={fleetHistoryRecords}
              versionMap={versionMap}
              onFleetRefresh={refreshAllFleetAndStatus}
              onClear={() => setSelectedCarId("")}
            />
          </div>
        ) : (
          <p className="tiny subtle" style={{ padding: "0 16px 16px" }}>
            Click a vehicle row to open detail here.
          </p>
        )}
      </section>

      <CampaignManagementPanel onVersionMapChanged={refreshAllFleetAndStatus} />

      <details className="card legacy-advanced-section" style={{ marginTop: 12 }}>
        <summary className="card-header legacy-advanced-summary">
          <h2 style={{ display: "inline" }}>Fleet command history (raw JSONL)</h2>
          <span className="status-badge status-gray" style={{ marginLeft: 10 }}>
            Advanced · duplicate of Campaign Logs → Fleet History
          </span>
        </summary>
        <div style={{ padding: "0 8px 8px" }}>
          <FleetHistoryJsonlPanel records={fleetHistoryRecords} meta={fleetHistoryMeta} />
        </div>
      </details>

      <FleetDataDebugDetails
        vehicles={fleetVehicles}
        versionRows={versionMapRows}
        states={fleetStates}
        historyMeta={fleetHistoryMeta}
        canaryIds={canaryVehicleIds}
      />

      <section className="fleet-grid legacy-advanced-section">
        <details className="card" style={{ gridColumn: "1 / -1", marginBottom: 0 }}>
          <summary className="card-header" style={{ cursor: "pointer", listStyle: "none" }}>
            <div>
              <h2 style={{ display: "inline" }}>Legacy fleet-deploy</h2>
              <span className="status-badge status-gray" style={{ marginLeft: 10 }}>
                Advanced · direct DeploymentMgr
              </span>
              <p className="tiny subtle" style={{ marginTop: 6 }}>
                Prefer Deployment Campaigns above for OTA demo. Expand only for low-level fleet-deploy tests.
              </p>
            </div>
          </summary>
          <div style={{ padding: "0 16px 16px" }}>
        <FleetDeployPanel
          vehicles={fleetVehicles}
          versionMap={versionMap}
          stateByCar={stateByCar}
          onDeploy={onDeploy}
          deploying={fleetDeploying}
          deployError={fleetDeployError}
        />
        <FleetSummary summary={fleetSummary} rawStdout={fleetDeployStdout} rawStderr={fleetDeployStderr} />
          </div>
        </details>
      </section>
    </main>
  );
}
