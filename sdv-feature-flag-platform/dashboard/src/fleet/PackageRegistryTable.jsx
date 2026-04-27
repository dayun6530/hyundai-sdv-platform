import { useState } from "react";
import { assignVersionMapSlot } from "../api/fleet.js";
import { slotAssignmentLabel } from "./packageRegistryModel.js";

function defaultSlotForPackage(pkg) {
  if (pkg?.role === "stable") return "A";
  return "B";
}

function AssignSlotControls({ pkg, onAssigned }) {
  const [slot, setSlot] = useState(() => defaultSlotForPackage(pkg));
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState("");
  const [err, setErr] = useState("");

  const runAssign = async () => {
    setErr("");
    setMsg("");
    let warn = "";
    if (pkg.role === "candidate" && slot === "A") {
      warn = "\n\nWarning: assigning a candidate package to A slot (A is usually stable).";
    } else if (pkg.role === "stable" && slot === "B") {
      warn = "\n\nWarning: assigning a stable package to B slot (B is often used for candidate).";
    }
    const ok = window.confirm(
      `Assign ${pkg.feature} ${pkg.version} (package ${pkg.package_id}) to ${slot} slot?\n` +
        `This will update deploy/version_map.json.\n` +
        `It will not deploy to vehicles or change deploy/state.${warn}`,
    );
    if (!ok) return;
    setBusy(true);
    try {
      await assignVersionMapSlot({ package_id: pkg.package_id, slot });
      setMsg(
        `Assigned ${pkg.feature} ${pkg.version} to ${slot} slot. Campaign/Rollout using ${pkg.feature} + ${slot} will show ${pkg.version}.`,
      );
      await onAssigned?.();
    } catch (e) {
      setErr(e.message || "Assign failed");
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="pkg-assign-cell">
      <div className="pkg-assign-row">
        <select className="select pkg-assign-select" value={slot} onChange={(e) => setSlot(e.target.value)} disabled={busy}>
          <option value="A">A</option>
          <option value="B">B</option>
        </select>
        <button type="button" className="btn btn-primary pkg-assign-btn" disabled={busy} onClick={runAssign}>
          {busy ? "Assigning…" : "Assign to slot"}
        </button>
      </div>
      {err ? <div className="tiny pkg-assign-err">{err}</div> : null}
      {msg ? <div className="tiny pkg-assign-ok">{msg}</div> : null}
    </div>
  );
}

export default function PackageRegistryTable({ packages, versionMapRaw, onAssigned }) {
  const list = packages || [];

  return (
    <section className="card pkg-registry-card">
      <div className="card-header">
        <div>
          <h2>Package Registry</h2>
          <p className="tiny subtle">
            <strong>Package Registry</strong>: all registered packages. Use <strong>Assign to slot</strong> to update{" "}
            <code className="mono">deploy/version_map.json</code> (A/B only — no auto deploy, no state change).{" "}
            <strong>Version Map</strong> (left): current slot assignments. Campaign/Rollout reads slot versions from the version map after refresh.
          </p>
        </div>
        <span className="subtle">from `deploy/package_registry.json`</span>
      </div>
      {!list.length ? (
        <div className="empty-state">No packages registered yet.</div>
      ) : (
        <div className="table-wrap pkg-table-wrap">
          <table className="pkg-registry-table">
            <thead>
              <tr>
                <th>package_id</th>
                <th>feature</th>
                <th>version</th>
                <th>role</th>
                <th>binary</th>
                <th>compatible_models</th>
                <th>compatible_regions</th>
                <th>registered_at</th>
                <th>package_path</th>
                <th>version_map slot</th>
                <th>Assign to slot</th>
              </tr>
            </thead>
            <tbody>
              {list.map((p) => (
                <tr key={p.package_id}>
                  <td className="mono tiny">{p.package_id}</td>
                  <td>{p.feature}</td>
                  <td className="mono">{p.version}</td>
                  <td>{p.role}</td>
                  <td className="mono tiny">{p.binary}</td>
                  <td className="tiny">{(p.compatible_models || []).join(", ")}</td>
                  <td className="tiny">{(p.compatible_regions || []).join(", ")}</td>
                  <td className="mono tiny">{p.registered_at || "—"}</td>
                  <td className="mono tiny">{p.package_path || "—"}</td>
                  <td>
                    <span className="pkg-slot-badge">{slotAssignmentLabel(p, versionMapRaw)}</span>
                  </td>
                  <td>
                    <AssignSlotControls pkg={p} onAssigned={onAssigned} />
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </section>
  );
}
