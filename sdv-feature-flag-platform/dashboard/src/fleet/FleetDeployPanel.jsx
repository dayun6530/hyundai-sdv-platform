import { useEffect, useMemo, useState } from "react";
import { previewFleetCommand } from "../api/fleet.js";
import { computeOtaSafetyFleet, getEffectiveVehicleStatus } from "./fleetDashboardModel.js";
import {
  buildFleetDeployCliPreview,
  carIdOptions,
  DEPLOYMENT_ROLES,
  getVersionMapSlotEntry,
  matchVehiclesForScope,
} from "./rolloutModel.js";
import { legacySlotToLabel, roleToLegacySlot } from "./roleMapping.js";

const FEATURES = ["ADAS", "Navigation", "Energy"];
const SCOPES = [
  { value: "vehicle", label: "vehicle (single car)" },
  { value: "group", label: "group" },
  { value: "region", label: "region" },
  { value: "group_region", label: "group + region" },
];
const GROUP_OPTIONS = ["canary", "beta", "stable"];
const REGION_OPTIONS = ["KR", "US", "EU"];

export default function FleetDeployPanel({
  vehicles,
  versionMap,
  stateByCar,
  onDeploy,
  deploying,
  deployError,
}) {
  const [feature, setFeature] = useState("ADAS");
  const [deploymentRole, setDeploymentRole] = useState("candidate");
  const slot = roleToLegacySlot(deploymentRole);
  const [scope, setScope] = useState("group_region");
  const [carId, setCarId] = useState("car_1");
  const [group, setGroup] = useState("canary");
  const [region, setRegion] = useState("KR");
  const [syncConfig, setSyncConfig] = useState(true);
  const [commandPreviewBusy, setCommandPreviewBusy] = useState(false);
  const [commandPreviewError, setCommandPreviewError] = useState("");
  const [commandPreviewResult, setCommandPreviewResult] = useState(null);

  const raw = versionMap?.raw ?? versionMap;
  const slotEntry = useMemo(() => getVersionMapSlotEntry(raw, feature, slot), [raw, feature, slot]);
  const targetVersion = slotEntry?.version || "";

  const matched = useMemo(
    () => matchVehiclesForScope(vehicles, scope, { carId, group, region }),
    [vehicles, scope, carId, group, region],
  );

  const carsForSelect = useMemo(() => carIdOptions(vehicles), [vehicles]);

  useEffect(() => {
    if (carsForSelect.length && !carsForSelect.includes(carId)) {
      setCarId(carsForSelect[0]);
    }
  }, [carsForSelect, carId]);

  const cliPreview = useMemo(
    () =>
      buildFleetDeployCliPreview({
        scope,
        carId,
        group,
        region,
        feature,
        slot,
        version: targetVersion || "<version>",
        syncConfig,
      }),
    [scope, carId, group, region, feature, slot, targetVersion, syncConfig],
  );

  const hasNonReadySafety = useMemo(
    () =>
      matched.some((v) => {
        const st = stateByCar[v.car_id];
        const lvl = computeOtaSafetyFleet(!st || st.missing ? null : st).level;
        return lvl !== "READY";
      }),
    [matched, stateByCar],
  );
  const hasMissingState = useMemo(() => matched.some((v) => !stateByCar[v.car_id] || stateByCar[v.car_id].missing), [matched, stateByCar]);

  const canRollout = matched.length > 0 && Boolean(targetVersion);

  const commandPreviewVehicleId = useMemo(() => {
    if (scope === "vehicle") return carId;
    // TODO: extend command-preview API/UI to preview group/region targets directly.
    return matched[0]?.car_id || carsForSelect[0] || carId || "car_1";
  }, [scope, carId, matched, carsForSelect]);

  const commandPreviewPayload = useMemo(
    () => ({
      action: "deploy-candidate",
      vehicle_id: commandPreviewVehicleId,
      feature,
      version: targetVersion,
    }),
    [commandPreviewVehicleId, feature, targetVersion],
  );

  useEffect(() => {
    setCommandPreviewError("");
    setCommandPreviewResult(null);
  }, [commandPreviewPayload]);

  const commandPreview = commandPreviewResult?.preview || null;
  const legacyCommandPreview = Array.isArray(commandPreview?.legacy_command_preview)
    ? commandPreview.legacy_command_preview
    : [];
  const commandPreviewNotes = Array.isArray(commandPreview?.notes) ? commandPreview.notes : [];

  const runCommandPreview = async () => {
    if (!canRollout || !commandPreviewVehicleId || commandPreviewBusy) return;
    setCommandPreviewBusy(true);
    setCommandPreviewError("");
    try {
      const data = await previewFleetCommand(commandPreviewPayload);
      setCommandPreviewResult(data);
      if (data?.success === false || data?.preview?.ok === false) {
        setCommandPreviewError(data?.error || data?.preview?.error || "command preview failed");
      }
    } catch (e) {
      setCommandPreviewResult(null);
      setCommandPreviewError(e.message || "command preview failed");
    } finally {
      setCommandPreviewBusy(false);
    }
  };

  const runRollout = () => {
    if (!canRollout || deploying) return;
    const payload = { scope, feature, slot, version: targetVersion, sync_config: syncConfig };
    if (scope === "vehicle") payload.car_id = carId;
    if (scope === "group") payload.group = group;
    if (scope === "region") payload.region = region;
    if (scope === "group_region") {
      payload.group = group;
      payload.region = region;
    }
    onDeploy(payload);
  };

  return (
    <section className="card rollout-campaign-card">
      <div className="card-header">
        <div>
          <h2>Create Rollout Campaign</h2>
          <p className="rollout-subtitle subtle">
            Campaign-style form for <code className="mono">fleet-deploy</code> (no campaign file is saved in this step). Primary/Candidate versions come from{" "}
            <code className="mono">version_map.json</code>; packages registered under <code className="mono">deploy/package_registry.json</code> can be wired to slots in a later step.
          </p>
        </div>
      </div>

      {deployError ? <div className="error-banner">Rollout failed: {deployError}</div> : null}

      <div className="rollout-flow">
        <section className="rollout-block">
          <h3 className="rollout-step-title">1. Feature</h3>
          <div className="rollout-chip-row">
            {FEATURES.map((f) => (
              <label key={f} className={`rollout-chip ${feature === f ? "active" : ""}`}>
                <input type="radio" name="rollout-feature" value={f} checked={feature === f} onChange={() => setFeature(f)} />
                <span>{f}</span>
              </label>
            ))}
          </div>
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">2. Deployment role</h3>
          <p className="tiny rollout-hint">Candidate is recommended for validation rollouts; Primary is typically stable production.</p>
          <div className="rollout-chip-row">
            {DEPLOYMENT_ROLES.map((r) => (
              <label key={r.value} className={`rollout-chip ${deploymentRole === r.value ? "active" : ""}`}>
                <input
                  type="radio"
                  name="rollout-role"
                  value={r.value}
                  checked={deploymentRole === r.value}
                  onChange={() => setDeploymentRole(r.value)}
                />
                <span>{r.label}</span>
              </label>
            ))}
          </div>
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">3. Version map (read-only)</h3>
          <p className="tiny rollout-hint">
            Values come from <code className="mono">deploy/version_map.json</code> for the selected feature and{" "}
            {legacySlotToLabel(slot)} role.
          </p>
          <div className="rollout-readonly-grid">
            <div>
              <div className="label">version</div>
              <div className="value mono">{slotEntry?.version || "—"}</div>
            </div>
            <div>
              <div className="label">role</div>
              <div className="value">{slotEntry?.role || "—"}</div>
            </div>
            <div>
              <div className="label">binary</div>
              <div className="value mono tiny">{slotEntry?.binary || "—"}</div>
            </div>
          </div>
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">4. Target scope</h3>
          <select className="select rollout-select" value={scope} onChange={(e) => setScope(e.target.value)}>
            {SCOPES.map((o) => (
              <option key={o.value} value={o.value}>
                {o.label}
              </option>
            ))}
          </select>

          <div className="rollout-scope-fields">
            {scope === "vehicle" ? (
              <label className="rollout-field">
                <span>car_id</span>
                <select className="select" value={carId} onChange={(e) => setCarId(e.target.value)}>
                  {carsForSelect.map((id) => (
                    <option key={id} value={id}>
                      {id}
                    </option>
                  ))}
                </select>
              </label>
            ) : null}

            {scope === "group" || scope === "group_region" ? (
              <label className="rollout-field">
                <span>group</span>
                <select className="select" value={group} onChange={(e) => setGroup(e.target.value)}>
                  {GROUP_OPTIONS.map((g) => (
                    <option key={g} value={g}>
                      {g}
                    </option>
                  ))}
                </select>
              </label>
            ) : null}

            {scope === "region" || scope === "group_region" ? (
              <label className="rollout-field">
                <span>region</span>
                <select className="select" value={region} onChange={(e) => setRegion(e.target.value)}>
                  {REGION_OPTIONS.map((r) => (
                    <option key={r} value={r}>
                      {r}
                    </option>
                  ))}
                </select>
              </label>
            ) : null}
          </div>
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">5–6. Matched vehicles preview (OTA safety + approval)</h3>
          {!matched.length ? (
            <div className="rollout-empty subtle">No matched vehicles for selected scope.</div>
          ) : (
            <div className="table-wrap rollout-preview-wrap">
              <table className="rollout-preview-table">
                <thead>
                  <tr>
                    <th>Vehicle</th>
                    <th>Meta</th>
                    <th>{feature} (current → target)</th>
                    <th>OTA safety</th>
                    <th>Approval</th>
                    <th>Notes</th>
                  </tr>
                </thead>
                <tbody>
                  {matched.map((v) => {
                    const st = stateByCar[v.car_id];
                    const missing = !st || st.missing;
                    const safety = computeOtaSafetyFleet(missing ? null : st);
                    const feat = missing ? null : st.features?.[feature];
                    const curRoleLabel = feat?.role_labels?.active || legacySlotToLabel(feat?.active_slot) || "—";
                    const curVer = feat?.active_version ?? "—";
                    const tgt = `${legacySlotToLabel(slot)} / ${targetVersion || "—"}`;
                    const sameTarget =
                      !missing &&
                      String(feat?.active_slot) === String(slot) &&
                      String(feat?.active_version) === String(targetVersion);
                    const appr = missing ? "—" : st.ota?.driver_approved ? "Approved" : "Waiting Driver Approval";
                    const net = missing ? "—" : String(st.ota?.network ?? "—");
                    const effStatus = missing ? "missing" : getEffectiveVehicleStatus(st, v);

                    const notes = [];
                    if (sameTarget) notes.push("Already on target version");
                    if (!missing && safety.level === "BLOCKED") notes.push("Safety blocked — rollout may fail");

                    return (
                      <tr key={v.car_id}>
                        <td className="mono">
                          <strong>{v.car_id}</strong>
                          {missing ? <div className="fleet-missing-hint">STATE MISSING</div> : null}
                        </td>
                        <td className="tiny">
                          {v.model} / {v.region} / {v.group}
                          <div className="rollout-meta-line">
                            status: <span className="mono">{effStatus}</span>
                          </div>
                          <div className="rollout-meta-line">
                            network: <span className="mono">{net}</span>
                          </div>
                        </td>
                        <td className="tiny mono">
                          <div>
                            {feature}: {curRoleLabel} / {curVer} → {tgt}
                          </div>
                          <div className="rollout-meta-line">
                            Current: {curRoleLabel} / {curVer}
                          </div>
                          <div className="rollout-meta-line">
                            Target: {tgt}
                          </div>
                        </td>
                        <td>
                          <span
                            className={`ota-safety ${
                              safety.level === "READY"
                                ? "ota-ready"
                                : safety.level === "WAITING_APPROVAL"
                                  ? "ota-wait"
                                  : safety.level === "MISSING"
                                    ? "ota-missing"
                                    : "ota-blocked"
                            }`}
                          >
                            {safety.level}
                          </span>
                        </td>
                        <td>{appr}</td>
                        <td className="tiny">{notes.length ? notes.join(" · ") : "—"}</td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          )}

          {matched.length && hasNonReadySafety ? (
            <div className="rollout-banner warn">
              Some vehicles are not OTA safety <strong>READY</strong>. <code className="mono">fleet-deploy</code> still runs per
              vehicle; targets that fail gates will appear in <code className="mono">SUMMARY_JSON</code> / history.
            </div>
          ) : null}
          {matched.length && hasMissingState ? (
            <div className="rollout-banner warn">One or more matched vehicles have missing deploy/state — rollouts may fail for those IDs.</div>
          ) : null}
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">7. Sync config</h3>
          <label className="rollout-check">
            <input type="checkbox" checked={syncConfig} onChange={(e) => setSyncConfig(e.target.checked)} />
            <span>
              <code className="mono">--sync-config</code> after successful state deploy (rewrite <code className="mono">config/car/&lt;id&gt;.conf</code> from state).
              Recommended <strong>on</strong> for demos; you can disable.
            </span>
          </label>
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">8. CLI command preview</h3>
          <div className="json-box mono rollout-cli">{cliPreview}</div>
        </section>

        <section className="rollout-block rollout-actions">
          <h3 className="rollout-step-title">9. Rollout</h3>
          {!canRollout ? (
            <p className="rollout-empty subtle">No matched vehicles for selected scope.</p>
          ) : null}
          <div className="rollout-action-row" style={{ display: "flex", flexWrap: "wrap", gap: 10 }}>
            <button
              type="button"
              className="btn btn-secondary"
              onClick={runCommandPreview}
              disabled={!canRollout || !commandPreviewVehicleId || commandPreviewBusy}
            >
              {commandPreviewBusy ? "Previewing..." : "Preview Command"}
            </button>
            <button type="button" className="btn btn-primary" onClick={runRollout} disabled={!canRollout || deploying}>
              {deploying ? "Running rollout…" : "Run Rollout"}
            </button>
          </div>
          <p className="tiny rollout-hint" style={{ marginTop: 10 }}>
            Command preview calls <code className="mono">POST /api/fleet/command-preview</code> only. This is a dry-run
            preview. No deployment command is executed.
          </p>
          {scope !== "vehicle" ? (
            <p className="tiny subtle">
              Preview target uses the first matched vehicle <code className="mono">{commandPreviewVehicleId}</code> for this{" "}
              <code className="mono">{scope}</code> rollout scope.
            </p>
          ) : null}
          {commandPreviewError ? <div className="error-banner">{commandPreviewError}</div> : null}
          {commandPreview ? (
            <div className="va-result" style={{ marginTop: 12 }}>
              <div className="rollout-banner warn">
                <strong>This is a dry-run preview. No deployment command is executed.</strong>
              </div>
              <div className="rollout-readonly-grid" style={{ marginTop: 12 }}>
                <div>
                  <div className="label">dry_run</div>
                  <span className={`status-badge ${commandPreview.dry_run === true ? "status-green" : "status-red"}`}>
                    {String(commandPreview.dry_run)}
                  </span>
                </div>
                <div>
                  <div className="label">will_execute</div>
                  <span className={`status-badge ${commandPreview.will_execute === false ? "status-green" : "status-red"}`}>
                    {String(commandPreview.will_execute)}
                  </span>
                </div>
                <div>
                  <div className="label">action</div>
                  <div className="value mono">{commandPreview.action || "—"}</div>
                </div>
                <div>
                  <div className="label">vehicle_id</div>
                  <div className="value mono">{commandPreview.vehicle_id || "—"}</div>
                </div>
                <div>
                  <div className="label">feature</div>
                  <div className="value mono">{commandPreview.feature || "—"}</div>
                </div>
                <div>
                  <div className="label">version</div>
                  <div className="value mono">{commandPreview.version || "—"}</div>
                </div>
              </div>
              <div style={{ marginTop: 12 }}>
                <div className="label">legacy_command_preview</div>
                <div className="json-box mono">
                  {legacyCommandPreview.length
                    ? legacyCommandPreview
                        .map((item, idx) => {
                          const command = item.command || `${item.method || ""} ${item.endpoint || ""}`.trim();
                          const body = item.body ? `\n   body: ${JSON.stringify(item.body)}` : "";
                          const note = item.note ? `\n   note: ${item.note}` : "";
                          return `${idx + 1}. [${item.type || "legacy"}] ${command}${body}${note}`;
                        })
                        .join("\n\n")
                    : "—"}
                </div>
              </div>
              {commandPreviewNotes.length ? (
                <div style={{ marginTop: 12 }}>
                  <div className="label">notes</div>
                  <div className="json-box mono">{commandPreviewNotes.map((n) => `- ${n}`).join("\n")}</div>
                </div>
              ) : null}
            </div>
          ) : null}
          <p className="tiny rollout-hint">
            10–11. After execution, <strong>Deploy Summary</strong> shows <code className="mono">[SUMMARY_JSON]</code> and stdout/stderr.
            Fleet command history reloads from <code className="mono">deploy/history/fleet_history.jsonl</code>; the Fleet Dashboard refreshes
            state on the next poll (and immediately after this rollout).
          </p>
        </section>
      </div>
    </section>
  );
}
