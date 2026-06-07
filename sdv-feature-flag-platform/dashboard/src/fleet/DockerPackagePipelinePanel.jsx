import { useCallback, useEffect, useMemo, useState } from "react";
import {
  fetchDockerPipelinePreflight,
  fetchDockerPipelineSteps,
  runDockerPipelineStep,
} from "../api/dockerPackagePipeline.js";
import { carIdOptions } from "./rolloutModel.js";

const FEATURES = ["ADAS", "Navigation", "Energy"];

/** Demo core path: one button per terminal step (API fetch fails → still render). */
const DEMO_CORE_STEPS = [
  {
    id: "new_package",
    stepNum: 3,
    label: "Package skeleton",
    hint: "new_package.sh → deploy/packages/…/manifest.json",
  },
  {
    id: "build",
    stepNum: 4,
    label: "Build image",
    hint: "build_standalone_demo.sh (requires demo_src/*.cpp from step 2)",
  },
  {
    id: "smoke_test",
    stepNum: 5,
    label: "Smoke test",
    hint: "docker run … | head -8",
  },
  {
    id: "registry_push",
    stepNum: 6,
    label: "Registry push",
    hint: "local-registry + tag + push candidate image",
  },
  {
    id: "register",
    stepNum: 7,
    label: "Register + version_map",
    hint: "register_build_artifact.sh (includes old step 8: version_map slot B)",
  },
];

const DEMO_OPTIONAL_STEPS = [
  {
    id: "push_primary",
    stepNum: "6b",
    label: "Push primary image",
    hint: "Build/tag/push primary_version before candidate deploy",
  },
  {
    id: "bootstrap",
    stepNum: 11,
    label: "Vehicle bootstrap",
    hint: "ota_vehicle.sh bootstrap — Primary+Shadow same version on selected car",
  },
];

function statusBadge(ok) {
  if (ok === true) return "status-green";
  if (ok === false) return "status-red";
  return "status-gray";
}

function stepTitle(step) {
  return `Step ${step.stepNum} · ${step.label}`;
}

export default function DockerPackagePipelinePanel({ vehicles, onPipelineComplete }) {
  const carOptions = useMemo(() => carIdOptions(vehicles), [vehicles]);
  const [apiStepsLoaded, setApiStepsLoaded] = useState(false);
  const [feature, setFeature] = useState("Energy");
  const [version, setVersion] = useState("v5.0");
  const [registry, setRegistry] = useState("localhost:5000");
  const [carId, setCarId] = useState("car_10");
  const [primaryVersion, setPrimaryVersion] = useState("v4.0");
  const [bootstrapVersion, setBootstrapVersion] = useState("v4.0");
  const [preflight, setPreflight] = useState(null);
  const [busyStep, setBusyStep] = useState("");
  const [lastResult, setLastResult] = useState(null);
  const [completedSteps, setCompletedSteps] = useState(() => new Set());
  const [showVehiclePrep, setShowVehiclePrep] = useState(false);
  const [err, setErr] = useState("");
  const [refreshing, setRefreshing] = useState(false);

  useEffect(() => {
    fetchDockerPipelineSteps()
      .then(() => setApiStepsLoaded(true))
      .catch(() => setApiStepsLoaded(false));
  }, []);

  useEffect(() => {
    if (carOptions.length && !carOptions.includes(carId)) {
      setCarId(carOptions[0]);
    }
  }, [carOptions, carId]);

  const refreshPreflight = useCallback(async () => {
    try {
      const r = await fetchDockerPipelinePreflight(feature, version, registry);
      setPreflight(r.preflight || null);
    } catch {
      setPreflight(null);
    }
  }, [feature, version, registry]);

  const reloadPipelineMeta = useCallback(async () => {
    try {
      await fetchDockerPipelineSteps();
      setApiStepsLoaded(true);
    } catch {
      setApiStepsLoaded(false);
    }
    await refreshPreflight();
  }, [refreshPreflight]);

  const handleRefresh = useCallback(async () => {
    setRefreshing(true);
    setErr("");
    try {
      await reloadPipelineMeta();
    } finally {
      setRefreshing(false);
    }
  }, [reloadPipelineMeta]);

  useEffect(() => {
    refreshPreflight();
  }, [refreshPreflight]);

  const runStep = async (stepDef, runAll = false) => {
    setErr("");
    setBusyStep(runAll ? "run_all" : stepDef.id);
    setLastResult(null);
    try {
      const body = {
        step: runAll ? undefined : stepDef.id,
        run_all: runAll,
        feature,
        version,
        registry,
        car_id: carId,
        primary_version: primaryVersion,
        bootstrap_version: bootstrapVersion,
      };
      const r = await runDockerPipelineStep(body);
      const enriched = {
        ...r,
        step_label: runAll ? "Run 3–7 (all)" : stepTitle(stepDef),
        step_id: stepDef.id,
      };
      setLastResult(enriched);
      if (!r.ok) {
        setErr(r.error || r.stderr || `Step failed (exit ${r.exit_code ?? "?"})`);
      } else {
        if (runAll && Array.isArray(r.results)) {
          setCompletedSteps((prev) => {
            const next = new Set(prev);
            for (const item of r.results) {
              if (item.ok) next.add(item.step);
            }
            return next;
          });
        } else if (!runAll) {
          setCompletedSteps((prev) => new Set(prev).add(stepDef.id));
        }
        await refreshPreflight();
        if (r.version_map_synced || runAll || stepDef.id === "register") {
          await onPipelineComplete?.();
        }
      }
    } catch (e) {
      const msg = e.message || String(e);
      setErr(msg);
      if (msg.includes("404")) {
        setErr(`${msg} — status-server 재시작 후 다시 시도하세요 (/api/docker-pipeline).`);
      }
    } finally {
      setBusyStep("");
    }
  };

  const cppReady = Boolean(preflight?.cpp_exists);
  const busy = Boolean(busyStep);

  const renderStepRow = (step) => {
    const done = completedSteps.has(step.id);
    const running = busyStep === step.id;
    return (
      <div
        key={step.id}
        className="docker-pipeline-step-row"
        style={{
          display: "grid",
          gridTemplateColumns: "minmax(0, 1fr) auto",
          gap: 12,
          alignItems: "center",
          padding: "10px 12px",
          borderRadius: 8,
          border: "1px solid var(--border-subtle, rgba(255,255,255,0.08))",
          background: done ? "rgba(34, 197, 94, 0.06)" : "transparent",
        }}
      >
        <div>
          <div style={{ display: "flex", alignItems: "center", gap: 8, flexWrap: "wrap" }}>
            <span className="status-badge status-gray mono">Step {step.stepNum}</span>
            <strong className="tiny">{step.label}</strong>
            {done ? <span className="status-badge status-green">done</span> : null}
          </div>
          <p className="tiny subtle" style={{ margin: "4px 0 0" }}>
            {step.hint}
          </p>
        </div>
        <button
          type="button"
          className="btn btn-sm"
          disabled={busy}
          onClick={() => runStep(step)}
          style={{ minWidth: 88 }}
        >
          {running ? "Running…" : "Run"}
        </button>
      </div>
    );
  };

  return (
    <section className="card rollout-campaign-card docker-pipeline-card">
      <div className="card-header">
        <div>
          <h2>Docker Package Pipeline</h2>
          <p className="rollout-subtitle subtle">
            Demo: run steps <strong>3 → 7 one at a time</strong>. Steps 2 (cpp) and 9–10 (campaign/policy) stay
            manual. Deploy on a vehicle via Deployment Campaigns (Validate → Run Candidate → Promote). Steps 6b/11
            are hidden by default — Run Candidate bootstraps Primary when needed.
          </p>
        </div>
        <div className="campaign-header-actions">
          <button
            type="button"
            className="btn"
            onClick={handleRefresh}
            disabled={busy || refreshing}
          >
            {refreshing ? "Refreshing…" : "Refresh"}
          </button>
        </div>
      </div>

      <div className="pkg-form-grid" style={{ padding: "0 16px" }}>
        <label className="pkg-field">
          <span>Feature</span>
          <select className="select" value={feature} onChange={(e) => setFeature(e.target.value)} disabled={busy}>
            {FEATURES.map((f) => (
              <option key={f} value={f}>
                {f}
              </option>
            ))}
          </select>
        </label>
        <label className="pkg-field">
          <span>Candidate version</span>
          <input
            className="input mono"
            value={version}
            onChange={(e) => setVersion(e.target.value)}
            placeholder="v5.0"
            disabled={busy}
          />
        </label>
        <label className="pkg-field">
          <span>Registry</span>
          <input
            className="input mono"
            value={registry}
            onChange={(e) => setRegistry(e.target.value)}
            placeholder="localhost:5000"
            disabled={busy}
          />
        </label>
      </div>

      <div style={{ padding: "8px 16px 0" }} className="tiny">
        <span className={`status-badge ${cppReady ? "status-green" : "status-yellow"}`}>
          cpp {cppReady ? "found" : "missing"}
        </span>
        {!apiStepsLoaded ? (
          <span className="status-badge status-yellow" style={{ marginLeft: 8 }}>
            API offline — restart status-server
          </span>
        ) : null}
        {preflight?.cpp_path ? (
          <span className="mono subtle" style={{ marginLeft: 8 }}>
            {preflight.cpp_path.replace(/^.*sdv-ota-manager/, "sdv-ota-manager")}
          </span>
        ) : null}
      </div>

      <div style={{ padding: "12px 16px 0" }}>
        <div className="label tiny" style={{ marginBottom: 8 }}>
          Core demo path (3–7) — run in order
        </div>
        <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
          {DEMO_CORE_STEPS.map(renderStepRow)}
        </div>
      </div>

      <details
        className="campaign-advanced-details"
        style={{ margin: "12px 16px 0", padding: "0 16px" }}
        open={showVehiclePrep}
        onToggle={(e) => setShowVehiclePrep(e.currentTarget.open)}
      >
        <summary className="tiny subtle" style={{ cursor: "pointer", padding: "4px 0" }}>
          Optional vehicle prep (steps 6b &amp; 11) — show if Run Candidate fails
        </summary>
        <p className="tiny subtle" style={{ margin: "8px 0 12px" }}>
          Normally skip: pick the target car in Deployment Campaigns; Run Candidate auto-bootstraps Primary. Use 6b/11
          only when Primary image or containers need manual recovery.
        </p>
        <div className="pkg-form-grid" style={{ marginBottom: 12 }}>
          <label className="pkg-field">
            <span>Bootstrap car</span>
            <select className="select" value={carId} onChange={(e) => setCarId(e.target.value)} disabled={busy}>
              {carOptions.map((id) => (
                <option key={id} value={id}>
                  {id}
                </option>
              ))}
            </select>
          </label>
          <label className="pkg-field">
            <span>Primary version (6b)</span>
            <input
              className="input mono"
              value={primaryVersion}
              onChange={(e) => setPrimaryVersion(e.target.value)}
              placeholder="v4.0"
              disabled={busy}
            />
          </label>
          <label className="pkg-field">
            <span>Bootstrap version (11)</span>
            <input
              className="input mono"
              value={bootstrapVersion}
              onChange={(e) => setBootstrapVersion(e.target.value)}
              placeholder="v4.0"
              disabled={busy}
            />
          </label>
        </div>
        <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
          {DEMO_OPTIONAL_STEPS.map(renderStepRow)}
        </div>
      </details>

      <details className="campaign-advanced-details" style={{ margin: "12px 16px 0" }}>
        <summary className="tiny subtle" style={{ cursor: "pointer", padding: "4px 0" }}>
          Advanced · Run 3–7 in one click (skip for live demo)
        </summary>
        <button
          type="button"
          className="btn btn-sm"
          disabled={busy}
          style={{ marginTop: 8 }}
          onClick={() => runStep({ id: "run_all", stepNum: "3–7", label: "All core steps" }, true)}
        >
          {busyStep === "run_all" ? "Running all…" : "Run 3–7 at once"}
        </button>
      </details>

      {err ? (
        <div className="error-banner" style={{ margin: "12px 16px 0" }}>
          {err}
        </div>
      ) : null}

      {lastResult ? (
        <div style={{ padding: "12px 16px 16px" }}>
          <div className="va-result-head">
            <span className="subtle">Last run</span>
            <span className={`status-badge ${statusBadge(lastResult.ok)}`}>
              {lastResult.ok ? "success" : "failed"}
            </span>
            <span className="mono tiny subtle" style={{ marginLeft: 8 }}>
              {lastResult.step_label || lastResult.step_id || lastResult.step}
            </span>
          </div>
          {lastResult.command_preview ? (
            <div style={{ marginTop: 8 }}>
              <div className="label tiny">Command (WSL)</div>
              <div className="json-box mono tiny">{lastResult.command_preview}</div>
            </div>
          ) : null}
          {Array.isArray(lastResult.results) && lastResult.results.length > 1 ? (
            <div style={{ marginTop: 8 }}>
              <div className="label tiny">Steps</div>
              <ul className="tiny">
                {lastResult.results.map((r) => (
                  <li key={r.step}>
                    <span className={`status-badge ${statusBadge(r.ok)}`}>{r.step}</span>
                    <span className="subtle"> exit {r.exit_code}</span>
                  </li>
                ))}
              </ul>
            </div>
          ) : null}
          {lastResult.stdout ? (
            <div style={{ marginTop: 8 }}>
              <div className="label tiny">stdout</div>
              <pre className="json-box mono tiny" style={{ whiteSpace: "pre-wrap", maxHeight: 220, overflow: "auto" }}>
                {lastResult.stdout}
              </pre>
            </div>
          ) : null}
          {lastResult.stderr ? (
            <div style={{ marginTop: 8 }}>
              <div className="label tiny">stderr</div>
              <pre className="json-box mono tiny" style={{ whiteSpace: "pre-wrap", maxHeight: 120, overflow: "auto" }}>
                {lastResult.stderr}
              </pre>
            </div>
          ) : null}
        </div>
      ) : null}
    </section>
  );
}
