import { useCallback, useEffect, useState } from "react";
import { fetchDockerLogs, fetchFleetHistoryLogs } from "../api/campaigns.js";
import { formatDockerRuntimeSummary, runStatusClass } from "./campaignUiModel.js";
import { RawJsonDetails, StatusBadge } from "./campaignShared.jsx";

const LOG_TABS = [
  { id: "primary", label: "Primary Logs", hint: "replaces: docker logs car_1_agent_adas" },
  { id: "candidate", label: "Candidate Logs", hint: "replaces: docker logs car_1_agent_adas_shadow" },
  { id: "fleet-history", label: "Fleet History", hint: "replaces: tail deploy/history/fleet_history.jsonl" },
  { id: "run-json", label: "Campaign Run JSON", hint: "replaces: cat deploy/campaign_runs/run_*.json" },
];

const DEFAULT_TAIL = 60;

function DockerLogsView({ data, loading, role }) {
  if (loading) return <p className="tiny subtle">Loading logs…</p>;
  if (!data) return <p className="tiny subtle">No log data.</p>;

  const status = data.status || "ok";
  if (status === "not_found") {
    const msg =
      role === "candidate"
        ? "Candidate inactive — container not running (normal after a successful promote)."
        : "Primary container not found.";
    return <div className="campaign-logs-alert campaign-logs-alert-muted">{msg}</div>;
  }
  if (status === "error") {
    return (
      <div className="campaign-logs-alert campaign-logs-alert-error">
        {data.error || data.text || "Docker logs unavailable (is Docker running?)"}
      </div>
    );
  }

  const lineCount = Array.isArray(data.lines) ? data.lines.length : 0;
  return (
    <div>
      <dl className="campaign-kv campaign-kv-wide campaign-logs-meta">
        <dt>Container</dt>
        <dd className="mono tiny">{data.container || "—"}</dd>
        <dt>Status</dt>
        <dd>
          <StatusBadge text={status} className="status-green" />
        </dd>
        <dt>Lines</dt>
        <dd className="mono">{lineCount}</dd>
        <dt>Tail</dt>
        <dd className="mono">{data.tail ?? DEFAULT_TAIL}</dd>
      </dl>
      <pre className="campaign-log-box mono tiny">{data.text || "(empty)"}</pre>
    </div>
  );
}

function FleetHistoryView({ data, loading, error }) {
  if (loading) return <p className="tiny subtle">Loading fleet history…</p>;
  if (error) {
    return <div className="campaign-logs-alert campaign-logs-alert-error">{error}</div>;
  }
  const entries = data?.entries || [];
  if (!entries.length) {
    return <p className="empty-state tiny">No fleet history entries for this filter.</p>;
  }

  return (
    <div>
      <div className="table-wrap">
        <table className="campaign-runs-table campaign-history-table">
          <thead>
            <tr>
              <th>Time</th>
              <th>Action</th>
              <th>Feature</th>
              <th>Version</th>
              <th>Docker mode</th>
              <th>Docker result</th>
              <th>Result</th>
              <th>Deploy ID</th>
            </tr>
          </thead>
          <tbody>
            {entries.map((e, idx) => (
              <tr key={e.deploy_id || `${e.timestamp}-${idx}`}>
                <td className="tiny mono">{e.timestamp || "—"}</td>
                <td className="tiny">{e.action || "—"}</td>
                <td>{e.feature || "—"}</td>
                <td className="mono tiny">{e.version || e.target_version || "—"}</td>
                <td className="tiny mono">{e.docker_runtime_mode || "—"}</td>
                <td className="tiny">
                  {e.docker_runtime_result ? (
                    <StatusBadge text={e.docker_runtime_result} className={runStatusClass(e.docker_runtime_result)} />
                  ) : (
                    "—"
                  )}
                </td>
                <td className="tiny">
                  {e.result ||
                    (e.success != null && e.failed != null
                      ? `${e.success} ok / ${e.failed} fail`
                      : e.success_targets?.length
                        ? `${e.success_targets.length} ok`
                        : "—")}
                </td>
                <td className="mono tiny">{e.deploy_id || "—"}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {data?.raw_lines?.length ? (
        <RawJsonDetails label="Raw history lines" data={data.raw_lines} />
      ) : null}
    </div>
  );
}

function CampaignRunJsonView({ run, loading, runId }) {
  if (!runId) {
    return <p className="empty-state tiny">No run selected — click a row in Campaign Run History.</p>;
  }
  if (loading) return <p className="tiny subtle">Loading run detail…</p>;
  if (!run) return <p className="tiny subtle">Run detail not available.</p>;

  const agg = run.aggregate || {};
  const dr = run.docker_runtime || {};
  const firstPv = Array.isArray(run.per_vehicle) ? run.per_vehicle[0] : null;

  return (
    <div>
      <div className="vd-badge-row" style={{ marginBottom: 10 }}>
        <StatusBadge text={run.phase || "?"} className="status-blue" />
        <StatusBadge text={run.status || "?"} className={runStatusClass(run.status)} />
        {dr.mode ? <StatusBadge text={dr.mode} className="status-blue" /> : null}
        {firstPv?.docker_runtime_result ? (
          <StatusBadge
            text={`docker ${firstPv.docker_runtime_result}`}
            className={runStatusClass(firstPv.docker_runtime_result)}
          />
        ) : null}
      </div>
      <dl className="campaign-kv campaign-kv-wide">
        <dt>Run ID</dt>
        <dd className="mono tiny">{run.run_id}</dd>
        <dt>Aggregate</dt>
        <dd className="mono">
          {agg.success ?? 0} ok / {agg.failed ?? 0} failed / {agg.total ?? 0} total
        </dd>
        <dt>Docker image</dt>
        <dd className="mono tiny">{dr.image || firstPv?.docker_runtime_image || "—"}</dd>
        <dt>Docker runtime</dt>
        <dd className="tiny">{formatDockerRuntimeSummary(dr)}</dd>
      </dl>
      {Array.isArray(run.per_vehicle) && run.per_vehicle.length ? (
        <div className="table-wrap" style={{ marginTop: 8 }}>
          <table className="campaign-runs-table">
            <thead>
              <tr>
                <th>Vehicle</th>
                <th>Result</th>
                <th>Mode</th>
                <th>Docker</th>
                <th>Image</th>
              </tr>
            </thead>
            <tbody>
              {run.per_vehicle.map((pv) => (
                <tr key={pv.car_id}>
                  <td className="mono">{pv.car_id}</td>
                  <td>
                    <StatusBadge text={pv.result || "—"} className={runStatusClass(pv.result)} />
                  </td>
                  <td className="tiny mono">{pv.docker_runtime_mode || "—"}</td>
                  <td>
                    <StatusBadge
                      text={pv.docker_runtime_result || "—"}
                      className={runStatusClass(pv.docker_runtime_result)}
                    />
                  </td>
                  <td className="mono tiny">{pv.docker_runtime_image || "—"}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : null}
      <RawJsonDetails label="Raw run JSON" data={run} />
    </div>
  );
}

export default function CampaignLogsPanel({
  carId,
  feature,
  selectedRunId,
  selectedRun,
  runDetailLoading,
  refreshToken = 0,
  preferredTab = "",
  onPreferredTabApplied,
}) {
  const [activeTab, setActiveTab] = useState("primary");
  const [primaryLogs, setPrimaryLogs] = useState(null);
  const [candidateLogs, setCandidateLogs] = useState(null);
  const [fleetHistory, setFleetHistory] = useState(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  const loadPrimary = useCallback(async () => {
    if (!carId || !feature) return;
    setLoading(true);
    setError("");
    const res = await fetchDockerLogs(carId, feature, "primary", DEFAULT_TAIL);
    if (!res.ok) setError(res.error || "Failed to load primary logs");
    else setPrimaryLogs(res);
    setLoading(false);
  }, [carId, feature]);

  const loadCandidate = useCallback(async () => {
    if (!carId || !feature) return;
    setLoading(true);
    setError("");
    const res = await fetchDockerLogs(carId, feature, "candidate", DEFAULT_TAIL);
    if (!res.ok) setError(res.error || "Failed to load candidate logs");
    else setCandidateLogs(res);
    setLoading(false);
  }, [carId, feature]);

  const loadFleetHistory = useCallback(async () => {
    if (!carId) return;
    setLoading(true);
    setError("");
    const res = await fetchFleetHistoryLogs({ limit: 20, car_id: carId, feature });
    if (!res.ok) setError(res.error || "Failed to load fleet history");
    else setFleetHistory(res);
    setLoading(false);
  }, [carId, feature]);

  const refreshActive = useCallback(() => {
    if (activeTab === "primary") loadPrimary();
    else if (activeTab === "candidate") loadCandidate();
    else if (activeTab === "fleet-history") loadFleetHistory();
  }, [activeTab, loadPrimary, loadCandidate, loadFleetHistory]);

  const refreshAllLogs = useCallback(async () => {
    if (!carId || !feature) return;
    setLoading(true);
    setError("");
    const [pRes, cRes, fRes] = await Promise.all([
      fetchDockerLogs(carId, feature, "primary", DEFAULT_TAIL),
      fetchDockerLogs(carId, feature, "candidate", DEFAULT_TAIL),
      fetchFleetHistoryLogs({ limit: 20, car_id: carId, feature }),
    ]);
    if (!pRes.ok) setError(pRes.error || "Failed to load primary logs");
    else setPrimaryLogs(pRes);
    if (!cRes.ok && !pRes.ok) setError(cRes.error || "Failed to load candidate logs");
    else if (cRes.ok) setCandidateLogs(cRes);
    if (fRes.ok) setFleetHistory(fRes);
    setLoading(false);
  }, [carId, feature]);

  useEffect(() => {
    if (!carId) return;
    if (activeTab === "primary") loadPrimary();
    else if (activeTab === "candidate") loadCandidate();
    else if (activeTab === "fleet-history") loadFleetHistory();
  }, [activeTab, carId, feature, loadPrimary, loadCandidate, loadFleetHistory]);

  useEffect(() => {
    if (refreshToken <= 0) return;
    refreshAllLogs();
  }, [refreshToken, refreshAllLogs]);

  useEffect(() => {
    if (!preferredTab || !LOG_TABS.some((t) => t.id === preferredTab)) return;
    setActiveTab(preferredTab);
    onPreferredTabApplied?.();
  }, [preferredTab, onPreferredTabApplied]);

  useEffect(() => {
    setPrimaryLogs(null);
    setCandidateLogs(null);
    setFleetHistory(null);
    setError("");
  }, [carId, feature]);

  if (!carId) {
    return (
      <div className="card campaign-logs-card">
        <h3>Campaign Logs</h3>
        <p className="tiny subtle">Select a single-vehicle campaign to view primary / candidate logs and history.</p>
      </div>
    );
  }

  return (
    <div className="card campaign-logs-card">
      <div className="card-header">
        <div>
          <h3>Campaign Logs</h3>
          <p className="tiny subtle">
            {carId} · {feature} — tail {DEFAULT_TAIL} lines (read-only)
          </p>
        </div>
        {activeTab !== "run-json" ? (
          <button type="button" className="btn" onClick={refreshActive} disabled={loading}>
            {loading ? "Refreshing…" : "Refresh logs"}
          </button>
        ) : null}
      </div>

      <div className="campaign-log-tabs">
        {LOG_TABS.map((tab) => (
          <button
            key={tab.id}
            type="button"
            className={`btn campaign-log-tab ${activeTab === tab.id ? "campaign-log-tab-active" : ""}`}
            onClick={() => setActiveTab(tab.id)}
            title={tab.hint}
          >
            {tab.label}
          </button>
        ))}
      </div>

      {error && activeTab !== "run-json" ? (
        <div className="campaign-logs-alert campaign-logs-alert-error" style={{ marginTop: 10 }}>
          {error}
        </div>
      ) : null}

      <div className="campaign-logs-body">
        {activeTab === "primary" ? (
          <DockerLogsView data={primaryLogs} loading={loading} role="primary" />
        ) : null}
        {activeTab === "candidate" ? (
          <DockerLogsView data={candidateLogs} loading={loading} role="candidate" />
        ) : null}
        {activeTab === "fleet-history" ? (
          <FleetHistoryView data={fleetHistory} loading={loading} error={error} />
        ) : null}
        {activeTab === "run-json" ? (
          <CampaignRunJsonView run={selectedRun} loading={runDetailLoading} runId={selectedRunId} />
        ) : null}
      </div>
    </div>
  );
}
