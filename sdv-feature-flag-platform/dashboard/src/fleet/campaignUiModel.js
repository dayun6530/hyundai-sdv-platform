export function truncateMiddle(value, max = 42) {
  const s = String(value || "");
  if (s.length <= max) return s;
  const head = Math.ceil((max - 1) / 2);
  const tail = Math.floor((max - 1) / 2);
  return `${s.slice(0, head)}…${s.slice(-tail)}`;
}

export function formatTargetSelector(sel) {
  if (!sel || typeof sel !== "object") return "—";
  const scope = String(sel.scope || "").trim();
  if (scope === "vehicle" && Array.isArray(sel.vehicle_ids) && sel.vehicle_ids.length) {
    return `vehicle: ${sel.vehicle_ids.join(", ")}`;
  }
  if (scope === "group" && sel.group) return `group: ${sel.group}`;
  if (scope === "region" && sel.region) return `region: ${sel.region}`;
  if (scope === "group_region" && sel.group && sel.region) {
    return `group+region: ${sel.group} / ${sel.region}`;
  }
  return scope || "—";
}

export function formatPolicyFlow(stages) {
  if (!Array.isArray(stages) || !stages.length) return "—";
  return stages
    .map((s) => {
      const name = String(s?.stage_name || s?.deploy_action || s?.action || "").trim();
      if (name === "candidate" || s?.deploy_action === "candidate") return "candidate";
      if (name === "approval" || String(s?.action || "").includes("approval")) return "approval";
      if (name === "promote" || s?.deploy_action === "promote") return "promote";
      return name || "stage";
    })
    .join(" → ");
}

export function stageTitle(stage, index) {
  const action = String(stage?.deploy_action || stage?.action || "").toLowerCase();
  const name = String(stage?.stage_name || "").toLowerCase();
  if (action === "candidate" || name === "candidate") return `${index + 1}. Candidate Deploy`;
  if (action.includes("approval") || name === "approval") return `${index + 1}. Manual Approval`;
  if (action === "promote" || name === "promote") return `${index + 1}. Primary Promote`;
  return `${index + 1}. ${stage?.stage_name || stage?.stage_id || "Stage"}`;
}

export function runStatusClass(status) {
  const s = String(status || "").toLowerCase();
  if (s === "success") return "status-green";
  if (s === "failed") return "status-red";
  if (s === "partial") return "status-yellow";
  return "status-gray";
}

export function campaignStatusClass(status) {
  const s = String(status || "").toLowerCase();
  if (s === "active" || s === "running") return "status-blue";
  if (s === "draft") return "status-gray";
  if (s === "paused" || s === "failed") return "status-red";
  return "status-gray";
}

export function latestRunLabel(run) {
  if (!run) return { text: "no run", className: "status-gray" };
  const phase = run.phase || "?";
  const status = run.status || "?";
  return {
    text: `${phase} ${status}`,
    className: runStatusClass(status),
  };
}

export function buildLatestRunByCampaign(runs) {
  const map = new Map();
  for (const run of runs || []) {
    const cid = run?.campaign_id;
    if (!cid) continue;
    if (!map.has(cid)) map.set(cid, run);
  }
  return map;
}

export function inferSingleVehicleId(targetSelector, runs) {
  if (
    targetSelector?.scope === "vehicle" &&
    Array.isArray(targetSelector.vehicle_ids) &&
    targetSelector.vehicle_ids.length === 1
  ) {
    return targetSelector.vehicle_ids[0];
  }
  const latest = runs?.[0];
  if (Array.isArray(latest?.resolved_targets) && latest.resolved_targets.length === 1) {
    return latest.resolved_targets[0];
  }
  return null;
}

export function formatDockerRuntimeSummary(dr) {
  if (!dr) return "—";
  const parts = [];
  if (dr.mode) parts.push(dr.mode);
  if (dr.result) parts.push(dr.result);
  if (dr.action && dr.action !== dr.mode) parts.push(dr.action);
  return parts.length ? parts.join(" · ") : "—";
}

export function normalizeVersionToken(v) {
  const s = String(v || "").trim();
  if (!s) return "";
  return s[0] === "v" || s[0] === "V" ? s.slice(1).toLowerCase() : s.toLowerCase();
}

export function versionsMatch(a, b) {
  const na = normalizeVersionToken(a);
  const nb = normalizeVersionToken(b);
  return Boolean(na && nb && na === nb);
}

export function isRunSuccess(run) {
  return String(run?.status || "").toLowerCase() === "success";
}

export function findLatestSuccessfulCandidateRun(runs) {
  for (const run of runs || []) {
    if (String(run?.phase || "").toLowerCase() === "candidate" && isRunSuccess(run)) {
      return run;
    }
  }
  return null;
}

export function displayTargetRole(role) {
  const r = String(role || "").toLowerCase();
  if (r === "candidate" || r === "b") return "candidate";
  if (r === "primary" || r === "a") return "primary";
  return r || "—";
}
