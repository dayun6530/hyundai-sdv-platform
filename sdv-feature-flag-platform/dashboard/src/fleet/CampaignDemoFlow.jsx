const DEMO_STEPS = [
  { n: 1, title: "Validate", detail: "Check campaign readiness (OK / WARN / FAIL)." },
  { n: 2, title: "Run Candidate", detail: "Deploy candidate container; review Candidate Result." },
  { n: 3, title: "Candidate Logs", detail: "Campaign Logs → Candidate Logs (shadow tick)." },
  { n: 4, title: "Promote Primary", detail: "After candidate success; upgrades primary." },
  { n: 5, title: "Primary Logs", detail: "Campaign Logs → Primary Logs (primary tick)." },
  { n: 6, title: "Final state", detail: "Runtime Summary + Run History + Run JSON." },
];

const TERMINAL_MAP = [
  { terminal: "docker logs car_1_agent_adas", ui: "Campaign Logs → Primary Logs" },
  { terminal: "docker logs car_1_agent_adas_shadow", ui: "Campaign Logs → Candidate Logs" },
  { terminal: "cat deploy/campaign_runs/run_*.json", ui: "Campaign Logs → Campaign Run JSON" },
  { terminal: "tail deploy/history/fleet_history.jsonl", ui: "Campaign Logs → Fleet History" },
  { terminal: "cat deploy/vehicle_runtime/car_1.json", ui: "Vehicle Runtime Summary" },
  { terminal: "grep fleet_adas_version config/car/car_1.conf", ui: "Runtime Summary → Config synced badge" },
];

export default function CampaignDemoFlow({ defaultOpen = false }) {
  return (
    <details className="campaign-demo-flow" open={defaultOpen}>
      <summary className="campaign-demo-flow-summary">
        <strong>Demo flow</strong>
        <span className="tiny subtle">UI-only ADAS v0.5 · car_1 canary (no terminal required)</span>
      </summary>
      <ol className="campaign-demo-steps">
        {DEMO_STEPS.map((s) => (
          <li key={s.n}>
            <span className="campaign-demo-step-num">{s.n}</span>
            <span>
              <strong>{s.title}</strong>
              <span className="tiny subtle"> — {s.detail}</span>
            </span>
          </li>
        ))}
      </ol>
      <details className="campaign-terminal-map">
        <summary className="tiny subtle">Terminal → UI map</summary>
        <table className="campaign-terminal-table tiny">
          <thead>
            <tr>
              <th>Terminal</th>
              <th>UI</th>
            </tr>
          </thead>
          <tbody>
            {TERMINAL_MAP.map((row) => (
              <tr key={row.terminal}>
                <td className="mono">{row.terminal}</td>
                <td>{row.ui}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </details>
    </details>
  );
}
