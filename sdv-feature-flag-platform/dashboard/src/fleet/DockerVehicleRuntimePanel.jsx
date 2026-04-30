import { useCallback, useEffect, useMemo, useState } from "react";
import {
  fetchVehicleRuntimeDockerStatus,
  fetchVehicleRuntimeHealth,
  fetchVehicleRuntimeState,
  postVehicleRuntimeBootstrap,
  postVehicleRuntimeDeployShadow,
  postVehicleRuntimePromote,
} from "../api/vehicleRuntime.js";
import { carIdOptions } from "./rolloutModel.js";

const DOMAINS = [
  { value: "ADAS", label: "ADAS" },
  { value: "Navigation", label: "Navigation" },
  { value: "EvEnergy", label: "EvEnergy" },
];

function formatCmdPreview(carId, domain, version, sub, syncShadow) {
  const base = `ota_vehicle.sh ${sub} --car ${carId} --domain ${domain} --version ${version}`;
  if (sub === "promote" && syncShadow) {
    return `${base} --sync-shadow`;
  }
  return base;
}

function domainToRuntimeKey(domain) {
  const d = String(domain || "").trim();
  if (d === "ADAS") return "adas";
  if (d === "Navigation") return "navigation";
  if (d === "EvEnergy") return "evenergy";
  return d.toLowerCase();
}

export default function DockerVehicleRuntimePanel({ vehicles }) {
  const carsForSelect = useMemo(() => carIdOptions(vehicles), [vehicles]);
  const [carId, setCarId] = useState("car_1");
  const [domain, setDomain] = useState("ADAS");
  const [version, setVersion] = useState("1.0");
  const [syncShadowOnPromote, setSyncShadowOnPromote] = useState(false);

  const [health, setHealth] = useState(null);
  const [runtimeState, setRuntimeState] = useState(null);
  const [runtimeFile, setRuntimeFile] = useState("");

  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [lastOut, setLastOut] = useState({ stdout: "", stderr: "", exit_code: null, label: "" });

  useEffect(() => {
    if (carsForSelect.length && !carsForSelect.includes(carId)) {
      setCarId(carsForSelect[0]);
    }
  }, [carsForSelect, carId]);

  const loadHealth = useCallback(async () => {
    try {
      const h = await fetchVehicleRuntimeHealth();
      setHealth(h);
    } catch (e) {
      setHealth({ ok: false, script_found: false, error: e.message });
    }
  }, []);

  const loadRuntimeState = useCallback(async () => {
    try {
      const r = await fetchVehicleRuntimeState(carId);
      setRuntimeState(r.state ?? null);
      setRuntimeFile(r.file || "");
    } catch (e) {
      setRuntimeState(null);
      setRuntimeFile("");
      setError(e.message || "Failed to load runtime state");
    }
  }, [carId]);

  useEffect(() => {
    loadHealth();
  }, [loadHealth]);

  useEffect(() => {
    setError("");
    loadRuntimeState();
  }, [loadRuntimeState]);

  const runOp = async (label, fn) => {
    setBusy(true);
    setError("");
    setLastOut({ stdout: "", stderr: "", exit_code: null, label: "" });
    try {
      const r = await fn();
      setLastOut({
        stdout: r.stdout || "",
        stderr: r.stderr || "",
        exit_code: r.exit_code ?? null,
        label,
      });
      if (r.ok === false) {
        setError(r.stderr?.trim() || `Command failed (exit ${r.exit_code})`);
      }
      await loadRuntimeState();
    } catch (e) {
      setError(e.message || String(e));
    } finally {
      setBusy(false);
    }
  };

  const onBootstrap = () =>
    runOp("bootstrap", () => postVehicleRuntimeBootstrap({ car_id: carId, domain, version }));

  const onDeployShadow = () =>
    runOp("deploy-shadow", () => postVehicleRuntimeDeployShadow({ car_id: carId, domain, version }));

  const onPromote = () =>
    runOp("promote", () =>
      postVehicleRuntimePromote({
        car_id: carId,
        domain,
        version,
        sync_shadow: syncShadowOnPromote,
      }),
    );

  const onDockerStatus = () =>
    runOp("status", () => fetchVehicleRuntimeDockerStatus(carId, domain));

  const scriptMissing = health && health.script_found === false;

  const runtimeKey = useMemo(() => domainToRuntimeKey(domain), [domain]);
  const domainRuntime = runtimeState?.domains?.[runtimeKey];
  const primaryVer = domainRuntime?.primary_version;
  const shadowVer = domainRuntime?.shadow_version;
  const lastMsgKo = domainRuntime?.last_message_ko;

  return (
    <section className="card rollout-campaign-card">
      <div className="card-header">
        <div>
          <h2>Docker runtime — Primary / Shadow</h2>
          <p className="rollout-subtitle subtle">
            Calls <code className="mono">sdv-ota-manager/scripts/ota_vehicle.sh</code> via WSL (same as fleet deploy
            path). Requires Docker in WSL, images like <code className="mono">sdv/adas-feature:v1.0</code>.
          </p>
          <p className="rollout-subtitle subtle" style={{ marginTop: "0.35rem" }}>
            위 Fleet 표의 <strong>A/B 슬롯</strong>은 <code className="mono">deploy/state/*.json</code> + DeploymentMgr
            모델이고, 이 카드의 <strong>버전 숫자</strong>는 Docker 이미지 태그(
            <code className="mono">sdv/adas-feature:v1.0</code> 등)와 별개입니다.
          </p>
        </div>
      </div>

      {scriptMissing ? (
        <div className="rollout-banner warn">
          <strong>Script not found.</strong> Expected sibling repo{" "}
          <code className="mono">sdv-ota-manager/scripts/ota_vehicle.sh</code> next to{" "}
          <code className="mono">sdv-feature-flag-platform</code>.
        </div>
      ) : null}

      <div className="rollout-flow">
        <section className="rollout-block">
          <h3 className="rollout-step-title">1. Vehicle &amp; feature</h3>
          <div className="rollout-scope-fields">
            <label className="rollout-field">
              <span>car_id</span>
              <select className="select rollout-select" value={carId} onChange={(e) => setCarId(e.target.value)}>
                {carsForSelect.map((id) => (
                  <option key={id} value={id}>
                    {id}
                  </option>
                ))}
              </select>
            </label>
            <label className="rollout-field">
              <span>domain</span>
              <select className="select rollout-select" value={domain} onChange={(e) => setDomain(e.target.value)}>
                {DOMAINS.map((d) => (
                  <option key={d.value} value={d.value}>
                    {d.label}
                  </option>
                ))}
              </select>
            </label>
            <label className="rollout-field">
              <span>image version (tag suffix)</span>
              <input
                className="select rollout-select"
                value={version}
                onChange={(e) => setVersion(e.target.value)}
                placeholder="1.0"
                spellCheck={false}
              />
            </label>
          </div>
          <p className="rollout-hint subtle">
            Flow: <strong>Bootstrap</strong> (Primary+Shadow same version) → <strong>Deploy shadow</strong> (Shadow
            only) → <strong>Promote</strong> (Primary to target version). Rollback: promote with an older version.
          </p>
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">2. Commands</h3>
          <div className="rollout-meta-line subtle mono">
            {formatCmdPreview(carId, domain, version, "bootstrap", false)}
          </div>
          <div className="rollout-actions">
            <div className="rollout-action-row">
              <button type="button" className="btn" disabled={busy || scriptMissing} onClick={onBootstrap}>
                Bootstrap (primary + shadow)
              </button>
              <button type="button" className="btn" disabled={busy || scriptMissing} onClick={onDeployShadow}>
                Deploy to shadow only
              </button>
              <button type="button" className="btn" disabled={busy || scriptMissing} onClick={onPromote}>
                Promote primary
              </button>
              <button type="button" className="btn btn-secondary" disabled={busy || scriptMissing} onClick={onDockerStatus}>
                Docker status (inspect)
              </button>
              <button type="button" className="btn btn-secondary" disabled={busy} onClick={loadRuntimeState}>
                Refresh JSON state
              </button>
            </div>
            <label className="rollout-check">
              <input
                type="checkbox"
                checked={syncShadowOnPromote}
                onChange={(e) => setSyncShadowOnPromote(e.target.checked)}
              />
              <span>On promote, also restart shadow with same image (--sync-shadow)</span>
            </label>
          </div>
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">2b. 요약 (선택 도메인)</h3>
          {lastMsgKo ? (
            <div className="rollout-banner" style={{ marginBottom: "0.5rem" }}>
              <strong>마지막 작업:</strong> {lastMsgKo}
            </div>
          ) : (
            <p className="rollout-hint subtle">아직 이 차량/도메인에 대한 기록이 없거나 JSON에 요약 필드가 없습니다.</p>
          )}
          <p className="rollout-meta-line">
            <strong>Primary</strong> 이미지 태그 버전:{" "}
            <span className="mono">{primaryVer != null && primaryVer !== "" ? `v${primaryVer}` : "—"}</span>
            {" · "}
            <strong>Shadow</strong>:{" "}
            <span className="mono">{shadowVer != null && shadowVer !== "" ? `v${shadowVer}` : "—"}</span>
          </p>
        </section>

        <section className="rollout-block">
          <h3 className="rollout-step-title">3. Runtime state file</h3>
          {runtimeFile ? (
            <p className="rollout-meta-line subtle mono">
              {runtimeFile}
            </p>
          ) : null}
          <pre className="rollout-cli">{runtimeState ? JSON.stringify(runtimeState, null, 2) : "(no file yet)"}</pre>
        </section>

        {error ? (
          <div className="error-banner" style={{ marginTop: "0.5rem" }}>
            {error}
          </div>
        ) : null}

        {lastOut.label ? (
          <section className="rollout-block">
            <h3 className="rollout-step-title">Last command: {lastOut.label}</h3>
            {lastOut.exit_code !== 0 && lastOut.exit_code != null ? (
              <div className="rollout-banner warn">Non-zero exit: {String(lastOut.exit_code)}</div>
            ) : null}
            <p className="rollout-meta-line subtle">exit_code: {String(lastOut.exit_code)}</p>
            {lastOut.stderr ? (
              <>
                <div className="tiny subtle">stderr</div>
                <pre className="rollout-cli">{lastOut.stderr}</pre>
              </>
            ) : null}
            {lastOut.stdout ? (
              <>
                <div className="tiny subtle">stdout</div>
                <pre className="rollout-cli">{lastOut.stdout}</pre>
              </>
            ) : null}
          </section>
        ) : null}
      </div>
    </section>
  );
}
