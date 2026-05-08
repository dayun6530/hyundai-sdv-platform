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
  deployGlobalVersion,
  deployGroupVersion,
  deployModelVersions,
  deployRatioVersions,
  deployRegionVersions,
} from "./api/unleash";
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

  // ---- Region -> Version rollout controller (Option 1 UI requested by user) ----
  const [unleashProjectId, setUnleashProjectId] = useState("default");
  const [unleashEnv, setUnleashEnv] = useState("development");
  const [unleashFeature, setUnleashFeature] = useState("NavigationFeature");
  const [regionVersions, setRegionVersions] = useState({ KR: "1.0", US: "2.0", EU: "" });
  const [modelVersions, setModelVersions] = useState({});
  const [globalVersion, setGlobalVersion] = useState("");
  const [canaryVersion, setCanaryVersion] = useState("2.0");
  const [ratioSplits, setRatioSplits] = useState([
    { percent: 50, version: "2.0" },
    { percent: 50, version: "3.0" },
  ]);
  const [deployUiStatus, setDeployUiStatus] = useState({
    region: { state: "idle", text: "" },
    model: { state: "idle", text: "" },
    ratio: { state: "idle", text: "" },
    global: { state: "idle", text: "" },
    group: {
      canary: { state: "idle", text: "" },
      beta: { state: "idle", text: "" },
      stable: { state: "idle", text: "" },
    },
  });
  const [unleashBusy, setUnleashBusy] = useState(false);
  const [unleashError, setUnleashError] = useState("");
  const [unleashResult, setUnleashResult] = useState(null);

  const setStatus = useCallback((key, state, text) => {
    setDeployUiStatus((prev) => ({
      ...(prev || {}),
      [key]: { state, text },
    }));
  }, []);

  const setGroupStatus = useCallback((group, state, text) => {
    setDeployUiStatus((prev) => ({
      ...(prev || {}),
      group: {
        ...(prev?.group || {}),
        [group]: { state, text },
      },
    }));
  }, []);

  const modelOptions = useMemo(() => {
    return [...new Set((fleetVehicles || []).map((v) => String(v?.model || "").trim()).filter(Boolean))].sort();
  }, [fleetVehicles]);

  const groupCounts = useMemo(() => {
    const counts = { canary: 0, beta: 0, stable: 0 };
    for (const v of fleetVehicles || []) {
      const g = String(v?.group || "").trim();
      if (g && counts[g] !== undefined) counts[g] += 1;
    }
    return counts;
  }, [fleetVehicles]);

  useEffect(() => {
    if (!modelOptions.length) return;
    setModelVersions((prev) => {
      const next = { ...(prev || {}) };
      for (const m of modelOptions) {
        if (next[m] === undefined) next[m] = "";
      }
      // Drop keys that no longer exist
      for (const k of Object.keys(next)) {
        if (!modelOptions.includes(k)) delete next[k];
      }
      return next;
    });
  }, [modelOptions]);

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

  const runRegionVersionsDeploy = async () => {
    try {
      setUnleashError("");
      setUnleashResult(null);
      setStatus("region", "deploying", "배포중…");
      setUnleashBusy(true);
      const payload = {
        projectId: unleashProjectId,
        environment: unleashEnv,
        featureName: unleashFeature,
        versionsByRegion: regionVersions,
      };
      const r = await deployRegionVersions(payload);
      setUnleashResult(r);
      setStatus("region", "success", "배포가 완료되었습니다.");
      await refreshAllFleetAndStatus();
    } catch (err) {
      setUnleashError(err.message || "Region version deploy failed");
      setStatus("region", "error", "배포가 실패되었습니다.");
    } finally {
      setUnleashBusy(false);
    }
  };

  const runGlobalDeploy = async () => {
    try {
      setUnleashError("");
      setUnleashResult(null);
      setStatus("global", "deploying", "배포중…");
      setUnleashBusy(true);
      const payload = {
        projectId: unleashProjectId,
        environment: unleashEnv,
        featureName: unleashFeature,
        version: String(globalVersion || "").trim(),
      };
      const r = await deployGlobalVersion(payload);
      setUnleashResult(r);
      setStatus("global", "success", "배포가 완료되었습니다.");
      await refreshAllFleetAndStatus();
    } catch (err) {
      setUnleashError(err.message || "Global deploy failed");
      setStatus("global", "error", "배포가 실패되었습니다.");
    } finally {
      setUnleashBusy(false);
    }
  };

  const runModelVersionsDeploy = async () => {
    try {
      setUnleashError("");
      setUnleashResult(null);
      setStatus("model", "deploying", "배포중…");
      setUnleashBusy(true);
      const payload = {
        projectId: unleashProjectId,
        environment: unleashEnv,
        featureName: unleashFeature,
        versionsByModel: modelVersions,
      };
      const r = await deployModelVersions(payload);
      setUnleashResult(r);
      setStatus("model", "success", "배포가 완료되었습니다.");
      await refreshAllFleetAndStatus();
    } catch (err) {
      setUnleashError(err.message || "Model version deploy failed");
      setStatus("model", "error", "배포가 실패되었습니다.");
    } finally {
      setUnleashBusy(false);
    }
  };

  const ratioSum = useMemo(() => {
    return (ratioSplits || []).reduce((acc, s) => acc + (Number(s?.percent) || 0), 0);
  }, [ratioSplits]);

  const runRatioDeploy = async () => {
    try {
      setUnleashError("");
      setUnleashResult(null);
      setStatus("ratio", "idle", "");
      const splits = (ratioSplits || [])
        .map((s) => ({ percent: Number(s?.percent) || 0, version: String(s?.version || "").trim() }))
        .filter((s) => s.percent > 0 || s.version);

      const sum = splits.reduce((a, b) => a + (Number(b.percent) || 0), 0);
      if (sum !== 100) {
        setUnleashError(`비율 합계가 100%여야 합니다. (현재 ${sum}%)`);
        setStatus("ratio", "error", "배포가 실패되었습니다.");
        return;
      }
      if (!splits.length || splits.some((s) => !s.version || s.percent <= 0)) {
        setUnleashError("각 칸은 percent>0, version이 모두 필요합니다.");
        setStatus("ratio", "error", "배포가 실패되었습니다.");
        return;
      }

      setStatus("ratio", "deploying", "배포중…");
      setUnleashBusy(true);
      const payload = {
        projectId: unleashProjectId,
        environment: unleashEnv,
        featureName: unleashFeature,
        splits,
      };
      const r = await deployRatioVersions(payload);
      setUnleashResult(r);
      setStatus("ratio", "success", "배포가 완료되었습니다.");
      await refreshAllFleetAndStatus();
    } catch (err) {
      setUnleashError(err.message || "Ratio deploy failed");
      setStatus("ratio", "error", "배포가 실패되었습니다.");
    } finally {
      setUnleashBusy(false);
    }
  };

  const runCanaryStep = async (group) => {
    try {
      setUnleashError("");
      setUnleashResult(null);
      setGroupStatus(group, "deploying", "배포중…");
      const version = String(canaryVersion || "").trim();
      if (!version) {
        setUnleashError("버전을 입력해 주세요. (예: v2.0)");
        setGroupStatus(group, "error", "배포가 실패되었습니다.");
        return;
      }
      setUnleashBusy(true);
      const payload = {
        projectId: unleashProjectId,
        environment: unleashEnv,
        featureName: unleashFeature,
        group,
        version,
      };
      const r = await deployGroupVersion(payload);
      setUnleashResult(r);
      setGroupStatus(group, "success", "배포가 완료되었습니다.");
      await refreshAllFleetAndStatus();
    } catch (err) {
      setUnleashError(err.message || "Canary deploy failed");
      setGroupStatus(group, "error", "배포가 실패되었습니다.");
    } finally {
      setUnleashBusy(false);
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

      <FleetDashboard
        vehicles={fleetVehicles}
        stateByCar={stateByCar}
        loading={fleetBootLoading}
        onToggleApproval={toggleApproval}
        selectedCarId={selectedCarId}
        onSelectCarId={setSelectedCarId}
      />

      <section className="card" style={{ marginTop: 12 }}>
        <div className="card-header">
          <div>
            <h2>SDV Version Rollout Controller</h2>
            <p className="tiny subtle">
              원하는 기능을 고른 뒤, KR/US/EU별로 배포할 버전을 입력하고 <strong>배포</strong>를 누르면 해당 국가 차량들만 각각 promote 됩니다.
              빈칸은 이번 배포에서 제외됩니다. 마지막에 “전체 배포”로 전 차량을 특정 버전으로 맞출 수 있습니다.
            </p>
          </div>
          <span className="subtle">status-server → Unleash Admin API</span>
        </div>

        {unleashError ? <div className="error-banner">Unleash control error: {unleashError}</div> : null}

        <div style={{ padding: "0 16px 16px" }}>
          <div className="kv" style={{ marginTop: 8 }}>
            <div className="k">feature</div>
            <div className="v">
              <select className="select mono" value={unleashFeature} onChange={(e) => setUnleashFeature(e.target.value)}>
                <option value="NavigationFeature">NavigationFeature</option>
                <option value="AdasFeature">AdasFeature</option>
                <option value="EvEnergyFeature">EvEnergyFeature</option>
              </select>
            </div>
          </div>

          <details style={{ marginTop: 10 }}>
            <summary className="tiny subtle" style={{ cursor: "pointer" }}>
              Advanced: project/environment (기본값: default / development)
            </summary>
            <div className="kv" style={{ marginTop: 8 }}>
              <div className="k">project</div>
              <div className="v">
                <input
                  className="input mono"
                  value={unleashProjectId}
                  onChange={(e) => setUnleashProjectId(e.target.value)}
                />
              </div>
              <div className="k">environment</div>
              <div className="v">
                <input className="input mono" value={unleashEnv} onChange={(e) => setUnleashEnv(e.target.value)} />
              </div>
            </div>
          </details>

          <div className="two-col-grid" style={{ marginTop: 14, alignItems: "start" }}>
            <section>
              <div className="label">Region versions (empty = skip)</div>
              <div className="table-wrap" style={{ marginTop: 8 }}>
                <table>
                  <thead>
                    <tr>
                      <th>Region</th>
                      <th>Version (e.g. 1.0 or v1.0)</th>
                    </tr>
                  </thead>
                  <tbody>
                    {["KR", "US", "EU"].map((r) => (
                      <tr key={r}>
                        <td className="mono">
                          <strong>{r}</strong>
                        </td>
                        <td>
                          <input
                            className="input mono"
                            value={regionVersions?.[r] ?? ""}
                            onChange={(e) => setRegionVersions((prev) => ({ ...(prev || {}), [r]: e.target.value }))}
                            placeholder={r === "EU" ? "(skip)" : "e.g. v2.0"}
                          />
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>

              <div style={{ marginTop: 10 }}>
                <button
                  type="button"
                  className="btn btn-primary"
                  onClick={runRegionVersionsDeploy}
                  disabled={unleashBusy}
                >
                  {unleashBusy ? "Deploying…" : "국가별 버전 배포"}
                </button>
                {deployUiStatus.region?.text ? (
                  <div className={`tiny subtle ${deployUiStatus.region.state === "error" ? "text-error" : ""}`} style={{ marginTop: 6 }}>
                    {deployUiStatus.region.text}
                  </div>
                ) : null}
              </div>
            </section>

            <section>
              <div className="label">Model versions (empty = skip)</div>
              <div className="tiny subtle">차종별로 버전을 지정해서 한 번에 배포합니다.</div>
              <div className="table-wrap" style={{ marginTop: 8 }}>
                <table>
                  <thead>
                    <tr>
                      <th>Model</th>
                      <th>Version (e.g. 1.0 or v1.0)</th>
                    </tr>
                  </thead>
                  <tbody>
                    {(modelOptions || []).length ? (
                      modelOptions.map((m) => (
                        <tr key={m}>
                          <td className="mono">
                            <strong>{m}</strong>
                          </td>
                          <td>
                            <input
                              className="input mono"
                              value={modelVersions?.[m] ?? ""}
                              onChange={(e) => setModelVersions((prev) => ({ ...(prev || {}), [m]: e.target.value }))}
                              placeholder="(skip)"
                            />
                          </td>
                        </tr>
                      ))
                    ) : (
                      <tr>
                        <td className="mono" colSpan={2}>
                          (loading…)
                        </td>
                      </tr>
                    )}
                  </tbody>
                </table>
              </div>
              <div style={{ marginTop: 10 }}>
                <button
                  type="button"
                  className="btn btn-primary"
                  onClick={runModelVersionsDeploy}
                  disabled={unleashBusy}
                >
                  {unleashBusy ? "Deploying…" : "차종별 버전 배포"}
                </button>
                {deployUiStatus.model?.text ? (
                  <div className={`tiny subtle ${deployUiStatus.model.state === "error" ? "text-error" : ""}`} style={{ marginTop: 6 }}>
                    {deployUiStatus.model.text}
                  </div>
                ) : null}
              </div>
            </section>
          </div>

          <div className="two-col-grid" style={{ marginTop: 18, alignItems: "start" }}>
            <section>
              <div className="label">Ratio rollout (random)</div>
              <div className="tiny subtle">
                비율(%) 합계가 <strong>100%</strong>일 때만 배포됩니다. 배포 시 차량에 버전이 무작위로 할당되어 promote 됩니다.
              </div>

              <div className="table-wrap" style={{ marginTop: 8 }}>
                <table>
                  <thead>
                    <tr>
                      <th style={{ width: 120 }}>Percent</th>
                      <th>Version</th>
                      <th style={{ width: 90 }}> </th>
                    </tr>
                  </thead>
                  <tbody>
                    {(ratioSplits || []).map((row, idx) => (
                      <tr key={idx}>
                        <td>
                          <input
                            className="input mono"
                            type="number"
                            min={0}
                            max={100}
                            value={row?.percent ?? 0}
                            onChange={(e) => {
                              const v = Number(e.target.value);
                              setRatioSplits((prev) =>
                                (prev || []).map((r, i) =>
                                  i === idx ? { ...r, percent: Number.isFinite(v) ? v : 0 } : r,
                                ),
                              );
                            }}
                          />
                        </td>
                        <td>
                          <input
                            className="input mono"
                            value={row?.version ?? ""}
                            onChange={(e) => {
                              const v = e.target.value;
                              setRatioSplits((prev) =>
                                (prev || []).map((r, i) => (i === idx ? { ...r, version: v } : r)),
                              );
                            }}
                            placeholder="e.g. v2.0"
                          />
                        </td>
                        <td>
                          <button
                            type="button"
                            className="btn"
                            onClick={() => setRatioSplits((prev) => (prev || []).filter((_, i) => i !== idx))}
                            disabled={(ratioSplits || []).length <= 2 || unleashBusy}
                          >
                            Remove
                          </button>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>

              <div className="tiny subtle" style={{ marginTop: 6 }}>
                합계: <span className="mono">{ratioSum}%</span> {ratioSum === 100 ? "(OK)" : "(must be 100)"}
              </div>

              <div style={{ marginTop: 10, display: "flex", gap: 8, flexWrap: "wrap" }}>
                <button
                  type="button"
                  className="btn"
                  onClick={() => setRatioSplits((prev) => [...(prev || []), { percent: 0, version: "" }])}
                  disabled={unleashBusy}
                >
                  + Add split
                </button>
                <button
                  type="button"
                  className="btn btn-primary"
                  onClick={runRatioDeploy}
                  disabled={unleashBusy || ratioSum !== 100}
                >
                  {unleashBusy ? "Deploying…" : "비율 랜덤 배포"}
                </button>
              </div>
              {deployUiStatus.ratio?.text ? (
                <div className={`tiny subtle ${deployUiStatus.ratio.state === "error" ? "text-error" : ""}`} style={{ marginTop: 6 }}>
                  {deployUiStatus.ratio.text}
                </div>
              ) : null}
            </section>

            <section>
              <div className="label">Canary rollout (group → staged)</div>
              <div className="tiny subtle">
                먼저 canary에 배포 후 확인하고, 문제가 없으면 beta → stable 순으로 승급 배포합니다. (group은{" "}
                <code className="mono">vehicles.json</code> 기준)
              </div>
              <div className="kv" style={{ marginTop: 8 }}>
                <div className="k">version</div>
                <div className="v">
                  <input
                    className="input mono"
                    value={canaryVersion}
                    onChange={(e) => setCanaryVersion(e.target.value)}
                    placeholder="e.g. v2.0"
                  />
                </div>
              </div>
              <div className="tiny subtle" style={{ marginTop: 6 }}>
                groups: canary=<span className="mono">{groupCounts.canary}</span>, beta=
                <span className="mono">{groupCounts.beta}</span>, stable=<span className="mono">{groupCounts.stable}</span>
              </div>
              <div style={{ marginTop: 10, display: "flex", gap: 8, flexWrap: "wrap" }}>
                <button
                  type="button"
                  className="btn btn-primary"
                  onClick={() => runCanaryStep("canary")}
                  disabled={unleashBusy}
                >
                  {unleashBusy ? "Deploying…" : "canary에 배포"}
                </button>
                <button type="button" className="btn btn-primary" onClick={() => runCanaryStep("beta")} disabled={unleashBusy}>
                  {unleashBusy ? "Deploying…" : "beta에 배포"}
                </button>
                <button
                  type="button"
                  className="btn btn-primary"
                  onClick={() => runCanaryStep("stable")}
                  disabled={unleashBusy}
                >
                  {unleashBusy ? "Deploying…" : "stable에 배포"}
                </button>
              </div>
              <div style={{ marginTop: 6 }}>
                {["canary", "beta", "stable"].map((g) =>
                  deployUiStatus.group?.[g]?.text ? (
                    <div
                      key={g}
                      className={`tiny subtle ${deployUiStatus.group?.[g]?.state === "error" ? "text-error" : ""}`}
                      style={{ marginTop: 2 }}
                    >
                      {g}: {deployUiStatus.group?.[g]?.text}
                    </div>
                  ) : null,
                )}
              </div>
            </section>
          </div>

          <div style={{ marginTop: 18 }}>
            <div className="label">Global rollout (all cars)</div>
            <div className="tiny subtle">전 차량을 한 버전으로 맞춥니다.</div>
            <div className="kv" style={{ marginTop: 8 }}>
              <div className="k">version</div>
              <div className="v">
                <input
                  className="input mono"
                  value={globalVersion}
                  onChange={(e) => setGlobalVersion(e.target.value)}
                  placeholder="e.g. v3.0"
                />
              </div>
            </div>
            <div style={{ marginTop: 10 }}>
              <button
                type="button"
                className="btn btn-primary"
                onClick={runGlobalDeploy}
                disabled={unleashBusy || !String(globalVersion || "").trim()}
              >
                {unleashBusy ? "Deploying…" : "전체 배포"}
              </button>
              {deployUiStatus.global?.text ? (
                <div className={`tiny subtle ${deployUiStatus.global.state === "error" ? "text-error" : ""}`} style={{ marginTop: 6 }}>
                  {deployUiStatus.global.text}
                </div>
              ) : null}
            </div>
          </div>
        </div>

        {unleashResult ? (
          <div style={{ padding: "0 16px 16px" }}>
            <div className="label">Result</div>
            <div className="json-box mono">{JSON.stringify(unleashResult, null, 2)}</div>
          </div>
        ) : null}
      </section>

      <VehicleDetailPanel
        carId={selectedCarId}
        vehicles={fleetVehicles}
        stateByCar={stateByCar}
        historyRecords={fleetHistoryRecords}
        versionMap={versionMap}
        onFleetRefresh={refreshAllFleetAndStatus}
        onClear={() => setSelectedCarId("")}
      />

      <PackageUploadPanel onRegistered={reloadFleetData} vehicles={fleetVehicles} />

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
