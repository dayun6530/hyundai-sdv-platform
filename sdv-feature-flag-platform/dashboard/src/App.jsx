import { useEffect, useMemo, useState } from "react";
import { fetchStatus } from "./api/status";

const DOMAIN_ORDER = ["ADAS", "NAV", "ENERGY"];

const STATUS_CLASS = {
  ACTIVATED: "status-green",
  STAGED: "status-yellow",
  FAILED: "status-red",
  HEALTH_CHECK_FAILED: "status-red",
  ROLLED_BACK: "status-orange",
  STABLE: "status-blue",
};

function formatDate(value) {
  if (!value) {
    return "-";
  }
  return value.replace("T", " ");
}

function statusClassName(status) {
  return STATUS_CLASS[status] || "status-gray";
}

function historyBadgeClass(event) {
  if (event?.event_type === "health_check") {
    return event.status === "success" ? "status-green" : "status-red";
  }
  if (event?.event_type === "rollback") {
    return "status-orange";
  }
  if (event?.event_type === "select") {
    return "status-blue";
  }
  if (event?.event_type === "deploy") {
    return "status-deploy";
  }
  return "status-gray";
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
      Loading deployment status...
    </section>
  );
}

function Header({ warningCount, connected, lastUpdated, updating }) {
  return (
    <header className="page-header">
      <div>
        <h1>SDV Deployment / Release Dashboard</h1>
        <p>Read-only status board for deployment, release, health check, and rollback state.</p>
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

function DomainCard({ domain, record }) {
  return (
    <article className="card domain-card">
      <div className="card-header">
        <h2>{domain}</h2>
        <span className={`status-badge ${statusClassName(record.last_deployment_status)}`}>
          {record.last_deployment_status}
        </span>
      </div>
      <div className="card-grid">
        <div>
          <div className="label">Candidate</div>
          <div className="value mono">{record.candidate_version}</div>
        </div>
        <div>
          <div className="label">Active</div>
          <div className="value mono">{record.active_version}</div>
        </div>
        <div>
          <div className="label">Stable</div>
          <div className="value mono">{record.stable_version}</div>
        </div>
        <div>
          <div className="label">Rollback</div>
          <div className="value">{record.rollback_available ? "Available" : "No"}</div>
        </div>
      </div>
      <div className="subtle">
        Last transition: <span className="mono">{formatDate(record.last_deployed_at)}</span>
      </div>
    </article>
  );
}

function SelectionPanel({ selection }) {
  const values = selection?.values || {};
  return (
    <section className="card">
      <div className="card-header">
        <h2>Current Selection</h2>
      </div>
      <div className="selection-row">
        {DOMAIN_ORDER.map((domain) => (
          <div key={domain} className="selection-chip">
            <span className="selection-domain">{domain}</span>
            <span className="selection-value mono">{values[domain] || "-"}</span>
          </div>
        ))}
      </div>
    </section>
  );
}

function BinaryList({ binaries }) {
  return (
    <section className="card">
      <div className="card-header">
        <h2>Active Binaries</h2>
      </div>
      {binaries?.length ? (
        <ul className="simple-list">
          {binaries.map((binary) => (
            <li key={binary} className="mono">
              {binary}
            </li>
          ))}
        </ul>
      ) : (
        <div className="empty-state">No active binaries found.</div>
      )}
    </section>
  );
}

function HistoryList({ history }) {
  return (
    <section className="card">
      <div className="card-header">
        <h2>Recent History</h2>
      </div>
      {history?.length ? (
        <div className="history-list">
          {history.map((event, index) => (
            <div key={`${event.raw}-${index}`} className="history-item">
              <div className="history-top">
                <span className={`status-badge ${historyBadgeClass(event)}`}>
                  {event.event_type || "unknown"}
                </span>
                <span className="history-summary">{event.summary || "event"}</span>
                <span className="history-time mono">{formatDate(event.deployed_at)}</span>
              </div>
              <div className="history-meta">
                <span>feature: {event.feature || "-"}</span>
                <span>version: {event.version || "-"}</span>
                <span>status: {event.status || "-"}</span>
              </div>
              <div className="history-raw mono">{event.raw}</div>
            </div>
          ))}
        </div>
      ) : (
        <div className="empty-state">No recent history.</div>
      )}
    </section>
  );
}

function ManifestTable({ manifest }) {
  return (
    <section className="card">
      <div className="card-header">
        <h2>Recent Manifest</h2>
      </div>
      {manifest?.length ? (
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>Feature</th>
                <th>Version</th>
                <th>Exec</th>
                <th>Status</th>
                <th>Mode</th>
                <th>Deployed At</th>
              </tr>
            </thead>
            <tbody>
              {manifest.map((item, index) => (
                <tr key={`${item.raw}-${index}`}>
                  <td>{item.feature || "-"}</td>
                  <td className="mono">{item.version || "-"}</td>
                  <td className="mono">{item.exec || "-"}</td>
                  <td>{item.status || "-"}</td>
                  <td>{item.mode || "-"}</td>
                  <td className="mono">{formatDate(item.deployed_at)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : (
        <div className="empty-state">No recent manifest entries.</div>
      )}
    </section>
  );
}

export default function App() {
  const [data, setData] = useState(null);
  const [loading, setLoading] = useState(true);
  const [updating, setUpdating] = useState(false);
  const [error, setError] = useState("");
  const [lastUpdated, setLastUpdated] = useState("");

  useEffect(() => {
    let active = true;
    let intervalId = null;

    // In React StrictMode (dev only), this effect can appear to run twice during development.
    async function load(isInitial = false) {
      try {
        if (isInitial) {
          setLoading(true);
        } else {
          setUpdating(true);
        }
        const next = await fetchStatus();
        if (active) {
          // Keep existing data on transient polling failures, then recover naturally on next success.
          setData(next);
          setError("");
          setLastUpdated(new Date().toISOString().slice(0, 19));
        }
      } catch (err) {
        if (active) {
          setError(err.message || "Failed to load status");
        }
      } finally {
        if (active) {
          if (isInitial) {
            setLoading(false);
          }
          setUpdating(false);
        }
      }
    }

    load(true);
    intervalId = window.setInterval(() => {
      load(false);
    }, 3000);

    return () => {
      active = false;
      if (intervalId !== null) {
        window.clearInterval(intervalId);
      }
    };
  }, []);

  const inventoryMap = useMemo(() => data?.inventory_map || {}, [data]);

  return (
    <main className="app-shell">
      <Header
        warningCount={data?.warnings?.length || 0}
        connected={!error && (!loading || !!data)}
        lastUpdated={lastUpdated}
        updating={updating}
      />

      <LoadingBanner loading={loading && !data && !error} />
      {error ? <section className="error-banner">Failed to load /api/status: {error}</section> : null}
      <WarningBanner warnings={data?.warnings} />

      <section className="card intro-card">
        <div className="legend-title">Version field guide</div>
        <div className="legend-text">
          <span><strong>candidate</strong> = deployed candidate, not necessarily released yet</span>
          <span><strong>active</strong> = currently selected version</span>
          <span><strong>stable</strong> = currently confirmed stable version</span>
        </div>
      </section>

      <section className="domain-grid">
        {DOMAIN_ORDER.map((domain) => (
          <DomainCard
            key={domain}
            domain={domain}
            record={inventoryMap[domain] || {
              candidate_version: "-",
              active_version: "-",
              stable_version: "-",
              last_deployment_status: "UNKNOWN",
              rollback_available: false,
              last_deployed_at: null,
            }}
          />
        ))}
      </section>

      <section className="two-col-grid">
        <SelectionPanel selection={data?.selection} />
        <BinaryList binaries={data?.active_binaries} />
      </section>

      <section className="two-col-grid">
        <HistoryList history={data?.recent_history} />
        <ManifestTable manifest={data?.recent_manifest} />
      </section>
    </main>
  );
}
