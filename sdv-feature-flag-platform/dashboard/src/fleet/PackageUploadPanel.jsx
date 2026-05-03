import { useEffect, useMemo, useState } from "react";
import { registerPackage } from "../api/fleet.js";
import DockerVehicleRuntimePanel from "./DockerVehicleRuntimePanel.jsx";
import { FEATURE_DEFAULTS, KNOWN_MODELS, KNOWN_REGIONS } from "./packageRegistryModel.js";
import { carIdOptions } from "./rolloutModel.js";

function buildManifestFromForm(f) {
  const now = new Date().toISOString();
  const package_id =
    f.package_id.trim() ||
    `${f.feature}_${f.version}_${now.slice(0, 10).replace(/-/g, "")}_${now.slice(11, 19).replace(/:/g, "")}`;
  const package_path = `deploy/packages/${f.feature}/${f.version}`;
  const manifest_path = `${package_path}/manifest.json`;
  return {
    package_id,
    feature: f.feature,
    version: f.version.trim(),
    role: f.role,
    binary: f.binary.trim(),
    package_path,
    manifest_path,
    description: f.description.trim(),
    created_at: now,
    registered_at: now,
    compatible_models: f.models,
    compatible_regions: f.regions,
    requires_safety_check: f.requires_safety_check,
    install_policy: {
      allow_driving_download: f.allow_driving_download,
      install_requires: {
        vehicle_mode: "PARKED",
        ignition: "OFF",
        update_mode: "READY",
        gear: "P",
        speed: 0,
        parking_brake: "ON",
        min_battery: Number(f.min_battery) || 30,
        driver_approved: true,
        network: "CONNECTED",
        target_system_running: false,
      },
    },
    rollback: {
      fallback_slot: f.fallback_slot,
      fallback_required: true,
    },
  };
}

export default function PackageUploadPanel({ onRegistered, vehicles }) {
  const carOptions = useMemo(() => carIdOptions(vehicles), [vehicles]);
  const [deployCarId, setDeployCarId] = useState("car_1");
  const [feature, setFeature] = useState("ADAS");
  const [packageId, setPackageId] = useState("");
  const [version, setVersion] = useState(FEATURE_DEFAULTS.ADAS.version);
  const [role, setRole] = useState("candidate");
  const [binary, setBinary] = useState(FEATURE_DEFAULTS.ADAS.binary);
  const [description, setDescription] = useState(FEATURE_DEFAULTS.ADAS.description);
  const [modelsCsv, setModelsCsv] = useState(KNOWN_MODELS.join(", "));
  const [regionsCsv, setRegionsCsv] = useState(KNOWN_REGIONS.join(", "));
  const [requiresSafety, setRequiresSafety] = useState(true);
  const [allowDrivingDl, setAllowDrivingDl] = useState(true);
  const [minBattery, setMinBattery] = useState(30);
  const [fallbackSlot, setFallbackSlot] = useState("A");
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState("");
  const [err, setErr] = useState("");

  useEffect(() => {
    const d = FEATURE_DEFAULTS[feature];
    if (!d) return;
    setVersion(d.version);
    setBinary(d.binary);
    setDescription(d.description);
  }, [feature]);

  useEffect(() => {
    if (carOptions.length && !carOptions.includes(deployCarId)) {
      setDeployCarId(carOptions[0]);
    }
  }, [carOptions, deployCarId]);

  const models = useMemo(
    () =>
      modelsCsv
        .split(/[,;]/)
        .map((s) => s.trim())
        .filter(Boolean),
    [modelsCsv],
  );
  const regions = useMemo(
    () =>
      regionsCsv
        .split(/[,;]/)
        .map((s) => s.trim())
        .filter(Boolean),
    [regionsCsv],
  );

  const manifestPreview = useMemo(
    () =>
      buildManifestFromForm({
        package_id: packageId,
        feature,
        version,
        role,
        binary,
        description,
        models,
        regions,
        requires_safety_check: requiresSafety,
        allow_driving_download: allowDrivingDl,
        min_battery: minBattery,
        fallback_slot: fallbackSlot,
      }),
    [
      packageId,
      feature,
      version,
      role,
      binary,
      description,
      models,
      regions,
      requiresSafety,
      allowDrivingDl,
      minBattery,
      fallbackSlot,
    ],
  );

  const submit = async () => {
    setErr("");
    setMsg("");
    setBusy(true);
    try {
      const body = {
        package_id: packageId.trim() || undefined,
        feature,
        version: version.trim(),
        role,
        binary: binary.trim(),
        description: description.trim(),
        compatible_models: models,
        compatible_regions: regions,
        requires_safety_check: requiresSafety,
        allow_driving_download: allowDrivingDl,
        min_battery: minBattery,
        fallback_slot: fallbackSlot,
      };
      const r = await registerPackage(body);
      setMsg(
        `Registered ${r.package?.package_id}. Paths: ${r.package?.manifest_path} · ${r.registry_path || "deploy/package_registry.json"}`,
      );
      await onRegistered?.();
    } catch (e) {
      setErr(e.message || "Registration failed");
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className="card pkg-upload-card">
      <div className="card-header">
        <div>
          <h2>Package Upload / Version Registry</h2>
          <p className="tiny subtle">
            <strong>Registry:</strong> manifest JSON is validated and written under{" "}
            <code className="mono">sdv-ota-manager/platform/deploy/packages/&lt;Feature&gt;/&lt;Version&gt;/</code> (same
            tree as <code className="mono">package_registry.json</code>). <strong>ZIP upload</strong> can be added later
            without changing the schema.
          </p>
          <p className="tiny subtle" style={{ marginTop: "0.35rem" }}>
            <strong>Docker rollout:</strong> use <strong>Car</strong> with the same <strong>Feature</strong> /{" "}
            <strong>Version</strong> fields — <code className="mono">ota_vehicle.sh</code> in WSL (Primary/Shadow,
            separate from Fleet <code className="mono">deploy/state</code> A/B).
          </p>
        </div>
      </div>

      <div className="pkg-form-grid">
        <label className="pkg-field">
          <span>Feature</span>
          <select className="select" value={feature} onChange={(e) => setFeature(e.target.value)}>
            <option value="ADAS">ADAS</option>
            <option value="Navigation">Navigation</option>
            <option value="Energy">Energy</option>
          </select>
        </label>
        <label className="pkg-field">
          <span>Version</span>
          <input className="input" value={version} onChange={(e) => setVersion(e.target.value)} placeholder="v1.0" />
        </label>
        <label className="pkg-field">
          <span>Car (Docker deploy target)</span>
          <select className="select" value={deployCarId} onChange={(e) => setDeployCarId(e.target.value)}>
            {carOptions.map((id) => (
              <option key={id} value={id}>
                {id}
              </option>
            ))}
          </select>
        </label>
        <label className="pkg-field">
          <span>Role</span>
          <select className="select" value={role} onChange={(e) => setRole(e.target.value)}>
            <option value="candidate">candidate</option>
            <option value="stable">stable</option>
            <option value="experimental">experimental</option>
          </select>
        </label>
        <label className="pkg-field">
          <span>Binary name</span>
          <input className="input" value={binary} onChange={(e) => setBinary(e.target.value)} placeholder="AdasFeature_v1_0" />
        </label>
        <label className="pkg-field pkg-field-wide">
          <span>package_id (optional — auto if empty)</span>
          <input className="input mono" value={packageId} onChange={(e) => setPackageId(e.target.value)} placeholder="ADAS_v1.0_20260428" />
        </label>
      </div>

      <p className="tiny subtle" style={{ marginTop: "0.35rem", marginBottom: 0 }}>
        <strong>Docker Primary/Shadow:</strong> commands below use the same Feature / Version / Car as this form. With
        queuing, containers are <strong>not</strong> recreated until you click <strong>Apply queued Docker now</strong> or
        run a full <code className="mono">run_sdv.ps1</code> (status-server drains the queue after it starts).
      </p>

      <DockerVehicleRuntimePanel
        vehicles={vehicles}
        embed
        integrationMode
        integrationCompact
        queueMutatingDockerOps
        integrationFeature={feature}
        integrationVersion={version}
        integrationCarId={deployCarId}
      />

      <div className="pkg-form-grid">
        <label className="pkg-field pkg-field-wide">
          <span>Description</span>
          <textarea className="input pkg-textarea" rows={3} value={description} onChange={(e) => setDescription(e.target.value)} />
        </label>
        <label className="pkg-field pkg-field-wide">
          <span>Compatible models (comma-separated)</span>
          <input className="input" value={modelsCsv} onChange={(e) => setModelsCsv(e.target.value)} placeholder="IONIQ5, IONIQ6" />
        </label>
        <label className="pkg-field pkg-field-wide">
          <span>Compatible regions (comma-separated)</span>
          <input className="input" value={regionsCsv} onChange={(e) => setRegionsCsv(e.target.value)} placeholder="KR, US, EU" />
        </label>
        <label className="pkg-check">
          <input type="checkbox" checked={requiresSafety} onChange={(e) => setRequiresSafety(e.target.checked)} />
          <span>Requires safety check</span>
        </label>
        <label className="pkg-check">
          <input type="checkbox" checked={allowDrivingDl} onChange={(e) => setAllowDrivingDl(e.target.checked)} />
          <span>Allow driving download</span>
        </label>
        <label className="pkg-field">
          <span>Min battery (install_requires)</span>
          <input
            type="number"
            className="input"
            min={0}
            max={100}
            value={minBattery}
            onChange={(e) => setMinBattery(Number(e.target.value))}
          />
        </label>
        <label className="pkg-field">
          <span>Rollback fallback slot</span>
          <select className="select" value={fallbackSlot} onChange={(e) => setFallbackSlot(e.target.value)}>
            <option value="A">A</option>
            <option value="B">B</option>
          </select>
        </label>
      </div>

      <div className="pkg-preview-block">
        <div className="label">Manifest preview (sent to server)</div>
        <div className="json-box mono">{JSON.stringify(manifestPreview, null, 2)}</div>
      </div>

      {err ? <div className="error-banner pkg-banner">{err}</div> : null}
      {msg ? <div className="pkg-success-banner pkg-banner">{msg}</div> : null}

      <div className="pkg-actions">
        <button type="button" className="btn btn-primary" disabled={busy} onClick={submit}>
          {busy ? "Registering…" : "Register Package"}
        </button>
      </div>
    </section>
  );
}
