/**
 * Fleet dashboard metrics + OTA safety (client-side only).
 * OTA "Safety Ready" matches fleet rollout gate: update_mode === READY (not legacy NOT READY heuristic).
 */

const FEATURE_KEYS = ["ADAS", "Navigation", "Energy"];

/** Normalize version tokens for comparison (strip leading v, lowercase). */
export function normalizeFleetVersionToken(v) {
  let s = String(v ?? "").trim().toLowerCase();
  if (!s) return "";
  if (s.startsWith("v")) s = s.slice(1);
  return s;
}

/**
 * True when fleet shadow lane is enabled and Shadow version differs from Primary (deploy-shadow / pre-promote).
 */
export function featureShadowRolloutActive(feat) {
  if (!feat || typeof feat !== "object") return false;
  if (!Boolean(feat.shadow_enabled)) return false;
  const a = normalizeFleetVersionToken(feat.active_version);
  const b = normalizeFleetVersionToken(feat.shadow_version);
  return Boolean(a && b && a !== b);
}

export function anyFeatureShadowRollout(state) {
  if (!state?.features) return false;
  return FEATURE_KEYS.some((k) => featureShadowRolloutActive(state.features[k]));
}

export function anyFeatureKillSwitch(state) {
  if (!state?.features) return false;
  return FEATURE_KEYS.some((k) => Boolean(state.features[k]?.kill_switch));
}

/** Admin fleet-shadow toggle (deploy/state only). */
export function anyFeatureShadowAdminFlag(state) {
  if (!state?.features) return false;
  return FEATURE_KEYS.some((k) => Boolean(state.features[k]?.shadow_enabled));
}

export function anyFeatureShadow(state) {
  return anyFeatureShadowRollout(state);
}

export function anyFeatureFlagOff(state) {
  if (!state?.features) return false;
  return FEATURE_KEYS.some((k) => (state.features[k]?.flag || "").toUpperCase() === "OFF");
}

function safetyCheckRows(ota) {
  const bat = Number(ota.battery || 0);
  const spd = Number(ota.speed || 0);
  return [
    {
      id: "vehicle_mode",
      pass: ota.vehicle_mode === "PARKED",
      label: "vehicle_mode === PARKED",
      detail: `value: ${String(ota.vehicle_mode ?? "—")}`,
    },
    {
      id: "ignition",
      pass: ota.ignition === "OFF",
      label: "ignition === OFF",
      detail: `value: ${String(ota.ignition ?? "—")}`,
    },
    {
      id: "update_mode",
      pass: ota.update_mode === "READY",
      label: "update_mode === READY",
      detail: `value: ${String(ota.update_mode ?? "—")}`,
    },
    { id: "gear", pass: ota.gear === "P", label: "gear === P", detail: `value: ${String(ota.gear ?? "—")}` },
    {
      id: "speed",
      pass: spd === 0,
      label: "speed === 0",
      detail: `value: ${String(ota.speed ?? "—")}`,
    },
    {
      id: "parking_brake",
      pass: ota.parking_brake === "ON",
      label: "parking_brake === ON",
      detail: `value: ${String(ota.parking_brake ?? "—")}`,
    },
    {
      id: "battery",
      pass: bat >= 30,
      label: "battery >= 30",
      detail: `value: ${String(ota.battery ?? "—")}${bat >= 30 ? ` (>= 30)` : ` (< 30)`}`,
    },
    {
      id: "driver_approved",
      pass: Boolean(ota.driver_approved) === true,
      label: "driver_approved === true",
      detail: `value: ${ota.driver_approved ? "true" : "false"}`,
    },
    {
      id: "network",
      pass: ota.network === "CONNECTED",
      label: "network === CONNECTED",
      detail: `value: ${String(ota.network ?? "—")}`,
    },
    {
      id: "target_system_running",
      pass: Boolean(ota.target_system_running) === false,
      label: "target_system_running === false",
      detail: `value: ${ota.target_system_running ? "true" : "false"}`,
    },
  ];
}

/**
 * @returns {{ level: 'READY'|'WAITING_APPROVAL'|'BLOCKED'|'MISSING', items: {id:string,pass:boolean,label:string,detail:string}[], failed: {id:string,label:string}[], tooltip: string, rolloutReadiness: string }}
 */
export function getOtaSafetyChecklist(state) {
  if (!state || state.missing) {
    return {
      level: "MISSING",
      items: [],
      failed: [],
      tooltip: "No deploy/state entry for this vehicle",
      rolloutReadiness: "MISSING",
    };
  }
  const ota = state.ota || {};
  const items = safetyCheckRows(ota);
  const failed = items.filter((c) => !c.pass).map((c) => ({ id: c.id, label: c.label }));
  const tooltip = items
    .filter((c) => !c.pass)
    .map((c) => `${c.label} (${c.detail})`)
    .join(", ");
  if (!failed.length) {
    return {
      level: "READY",
      items,
      failed: [],
      tooltip: "All OTA safety gates satisfied",
      rolloutReadiness: "READY",
    };
  }
  const onlyDriver = failed.length === 1 && failed[0].id === "driver_approved";
  if (onlyDriver) {
    return { level: "WAITING_APPROVAL", items, failed, tooltip, rolloutReadiness: "WAITING APPROVAL" };
  }
  return { level: "BLOCKED", items, failed, tooltip, rolloutReadiness: "BLOCKED" };
}

/**
 * @returns {{ level: 'READY'|'WAITING_APPROVAL'|'BLOCKED'|'MISSING', failed: {id:string,label:string,value:string}[], tooltip: string }}
 */
/**
 * Connectivity for fleet cards / filters: prefer WSL pgrep result when present.
 * @param {any} state
 * @param {any} vehicle registry row (optional)
 * @returns {"missing"|"online"|"offline"|string}
 */
export function getEffectiveVehicleStatus(state, vehicle) {
  if (!state || state.missing) return "missing";
  if (typeof state.runtime?.car_process_running === "boolean") {
    return state.runtime.car_process_running ? "online" : "offline";
  }
  return state.status || vehicle?.status || "unknown";
}

export function computeOtaSafetyFleet(state) {
  const full = getOtaSafetyChecklist(state);
  if (full.level === "MISSING") {
    return { level: "MISSING", failed: [], tooltip: full.tooltip };
  }
  const failed = full.items
    .filter((i) => !i.pass)
    .map((i) => ({
      id: i.id,
      label: i.label.replace(/ === /g, "=").replace(/ >= /g, ">="),
      value: i.detail.replace(/^value: /, ""),
    }));
  const tooltip =
    full.level === "READY" ? "All OTA safety gates satisfied" : failed.map((f) => `${f.label} (got ${f.value})`).join(", ");
  return { level: full.level, failed, tooltip };
}

export function computeFleetSummaryCards(vehicles, stateByCar) {
  const list = vehicles || [];
  let online = 0;
  let canary = 0;
  let beta = 0;
  let stable = 0;
  let killActive = 0;
  let shadowActive = 0;
  let waitingApproval = 0;
  let installCompleted = 0;
  let safetyReady = 0;

  for (const v of list) {
    const s = stateByCar[v.car_id];
    const miss = !s || Boolean(s.missing);
    const eff = getEffectiveVehicleStatus(s, v);
    const connected = !miss && eff === "online";

    if (connected) {
      online += 1;
      const g = v.group || "";
      if (g === "canary") canary += 1;
      if (g === "beta") beta += 1;
      if (g === "stable") stable += 1;
      const ota = s.ota || {};
      if (ota.driver_approved === false) waitingApproval += 1;
      const inst = String(ota.install_status || "").toLowerCase();
      if (inst.includes("completed") || inst.includes("installed")) installCompleted += 1;
      if (computeOtaSafetyFleet(s).level === "READY") safetyReady += 1;
      if (anyFeatureKillSwitch(s)) killActive += 1;
      if (anyFeatureShadowRollout(s)) shadowActive += 1;
    }
  }

  return {
    totalVehicles: list.length,
    onlineVehicles: online,
    canaryVehicles: canary,
    betaVehicles: beta,
    stableVehicles: stable,
    killSwitchActive: killActive,
    shadowModeActive: shadowActive,
    waitingApproval,
    installCompleted,
    safetyReady,
  };
}

export function filterFleetRows(vehicles, stateByCar, filters) {
  const fg = filters.group || "all";
  const fr = filters.region || "all";
  const fm = filters.model || "all";
  const fs = filters.status || "all";
  const fi = filters.featureIssue || "all";

  return (vehicles || []).filter((v) => {
    if (fg !== "all" && v.group !== fg) return false;
    if (fr !== "all" && v.region !== fr) return false;
    if (fm !== "all" && v.model !== fm) return false;

    const st = stateByCar[v.car_id];
    const miss = !st || Boolean(st.missing);
    const eff = getEffectiveVehicleStatus(st, v);
    if (fs === "missing") return miss;
    if (fs === "online") return !miss && eff === "online";
    if (fs === "offline") return !miss && eff === "offline";

    if (fi === "kill_active") return !miss && anyFeatureKillSwitch(st);
    if (fi === "shadow_active") return !miss && anyFeatureShadowRollout(st);
    if (fi === "flag_off") return !miss && anyFeatureFlagOff(st);

    return true;
  });
}
