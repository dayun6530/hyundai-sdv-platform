const DEMO_STEPS = [
  { n: 1, title: "Validate", detail: "Check campaign readiness (OK / WARN / FAIL)." },
  { n: 2, title: "Run Candidate", detail: "Deploy candidate container; review Candidate Result." },
  { n: 3, title: "Candidate Logs", detail: "Campaign Logs → Candidate Logs (shadow tick)." },
  { n: 4, title: "Promote Primary", detail: "After candidate success; upgrades primary." },
  { n: 5, title: "Primary Logs", detail: "Campaign Logs → Primary Logs (primary tick)." },
  { n: 6, title: "Final state", detail: "Runtime Summary + Run History + Run JSON." },
];

const FEATURE_AGENT_DOMAIN = {
  ADAS: "adas",
  Navigation: "navigation",
  Energy: "evenergy",
};

const FEATURE_CONFIG_KEY = {
  ADAS: "fleet_adas_version",
  Navigation: "fleet_navigation_version",
  Energy: "fleet_energy_version",
};

function agentDomain(feature) {
  return FEATURE_AGENT_DOMAIN[feature] || "adas";
}

function configKey(feature) {
  return FEATURE_CONFIG_KEY[feature] || "fleet_adas_version";
}

function vehicleGroup(vehicles, carId) {
  if (!carId) return "";
  const hit = (vehicles || []).find((v) => v.car_id === carId);
  return String(hit?.group || "").trim().toLowerCase();
}

function formatTargetLabel({ carId, carIds = [], group, vehicles }) {
  const ids = carIds.length ? carIds : carId ? [carId] : [];
  const groupKey = String(group || "").trim().toLowerCase();

  if (!ids.length) {
    return groupKey ? `Group ${groupKey}` : "select deploy target";
  }

  const inferredGroup =
    groupKey || (ids.length === 1 ? vehicleGroup(vehicles, ids[0]) : "");

  if (ids.length === 1) {
    return inferredGroup ? `${ids[0]} ${inferredGroup}` : ids[0];
  }

  const groupSuffix = inferredGroup ? ` · ${inferredGroup}` : "";
  if (ids.length <= 3) {
    return `${ids.join(", ")}${groupSuffix}`;
  }
  return `${ids.length} vehicles${groupSuffix}`;
}

export function buildDemoFlowSubtitle({
  feature,
  version,
  carId,
  carIds,
  group,
  vehicles,
  percentMode = false,
}) {
  if (percentMode) {
    return "UI-only · percent rollout (no terminal required)";
  }
  if (!feature || !version) {
    return "UI-only · select campaign and deploy target (no terminal required)";
  }
  const target = formatTargetLabel({ carId, carIds, group, vehicles });
  return `UI-only ${feature} ${version} · ${target} (no terminal required)`;
}

function buildTerminalMap({ carId, feature }) {
  const car = carId || "car_1";
  const domain = agentDomain(feature);
  const cfg = configKey(feature);
  return [
    {
      terminal: `docker logs ${car}_agent_${domain}`,
      ui: "Campaign Logs → Primary Logs",
    },
    {
      terminal: `docker logs ${car}_agent_${domain}_shadow`,
      ui: "Campaign Logs → Candidate Logs",
    },
    {
      terminal: "cat deploy/campaign_runs/run_*.json",
      ui: "Campaign Logs → Campaign Run JSON",
    },
    {
      terminal: "tail deploy/history/fleet_history.jsonl",
      ui: "Campaign Logs → Fleet History",
    },
    {
      terminal: `cat deploy/vehicle_runtime/${car}.json`,
      ui: "Vehicle Runtime Summary",
    },
    {
      terminal: `grep ${cfg} config/car/${car}.conf`,
      ui: "Runtime Summary → Config synced badge",
    },
  ];
}

export default function CampaignDemoFlow({
  defaultOpen = false,
  feature = "",
  version = "",
  carId = "",
  carIds = [],
  group = "",
  vehicles = [],
  percentMode = false,
}) {
  const subtitle = buildDemoFlowSubtitle({
    feature,
    version,
    carId,
    carIds,
    group,
    vehicles,
    percentMode,
  });
  const terminalMap = buildTerminalMap({ carId, feature });

  return (
    <details className="campaign-demo-flow" open={defaultOpen}>
      <summary className="campaign-demo-flow-summary">
        <strong>Demo flow</strong>
        <span className="tiny subtle">{subtitle}</span>
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
            {terminalMap.map((row) => (
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
