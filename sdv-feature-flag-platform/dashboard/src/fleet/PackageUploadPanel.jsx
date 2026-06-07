import { useEffect, useMemo, useState } from "react";
import { registerPackage } from "../api/fleet.js";
import DockerVehicleRuntimePanel from "./DockerVehicleRuntimePanel.jsx";
import { FEATURE_DEFAULTS, KNOWN_MODELS, KNOWN_REGIONS } from "./packageRegistryModel.js";
import { carIdOptions } from "./rolloutModel.js";
import { roleToLegacySlot } from "./roleMapping.js";

function buildManifestFromForm(f) {
  const now = new Date().toISOString();
  const package_id =
    f.package_id.trim() ||
    `${f.feature}_${f.version}_${now.slice(0, 10).replace(/-/g, "")}_${now.slice(11, 19).replace(/:/g, "")}`;
  const package_path = `deploy/packages/${f.feature}/${f.version}`;
  const manifest_path = `${package_path}/manifest.json`;
  const deployment_role = f.role === "stable" ? "primary" : "candidate";
  const fallback_role = f.fallback_role || "primary";
  return {
    package_id,
    feature: f.feature,
    version: f.version.trim(),
    role: f.role,
    deployment_role,
    default_role: deployment_role,
    slot_role: deployment_role,
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
      fallback_role,
      fallback_slot: roleToLegacySlot(fallback_role),
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
  const [fallbackRole, setFallbackRole] = useState("primary");
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
        fallback_role: fallbackRole,
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
      fallbackRole,
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
        fallback_role: fallbackRole,
        fallback_slot: roleToLegacySlot(fallbackRole),
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

      <details className="campaign-advanced-details" style={{ marginTop: 12 }}>
        <summary className="tiny subtle" style={{ cursor: "pointer", padding: "8px 0" }}>
          Advanced · Docker debug (ota_vehicle.sh direct) — use Campaign Management for demo
        </summary>
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
      </details>

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
          <span>Rollback fallback role</span>
          <select className="select" value={fallbackRole} onChange={(e) => setFallbackRole(e.target.value)}>
            <option value="primary">Primary</option>
            <option value="candidate">Candidate</option>
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
