import { useCallback, useEffect, useMemo, useState } from "react";
import {
  fetchVehicleRuntimeDockerStatus,
  fetchVehicleRuntimeHealth,
  fetchVehicleRuntimePendingQueue,
  fetchVehicleRuntimeState,
  postVehicleRuntimeApplyPending,
  postVehicleRuntimeBootstrap,
  postVehicleRuntimeDeployShadow,
  postVehicleRuntimePromote,
  postVehicleRuntimeQueueOperation,
} from "../api/vehicleRuntime.js";
import { normalizeOtaDockerVersionArg, pkgFeatureToOtaDomain } from "../dockerVersionUtils.js";
import { carIdOptions } from "./rolloutModel.js";

const DOMAINS = [
  { value: "ADAS", label: "ADAS" },
  { value: "Navigation", label: "Navigation" },
  { value: "EvEnergy", label: "EvEnergy" },
];

function formatCmdPreview(carId, domain, version, sub, syncShadow) {
  const vArg = normalizeOtaDockerVersionArg(version);
  const base = `ota_vehicle.sh ${sub} --car ${carId} --domain ${domain} --version ${vArg}`;
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

export default function DockerVehicleRuntimePanel({
  vehicles,
  embed = false,
  syncPackageFeature,
  syncPackageVersion,
  /** When set, domain/version come from the package form above (no duplicate inputs). */
  integrationMode = false,
  integrationFeature,
  integrationVersion,
  integrationCarId,
  /** With integrationMode: hide 요약 + runtime JSON (manifest above is enough). */
  integrationCompact = false,
  /** With integrationMode: bootstrap/shadow/promote enqueue; apply via button or run_sdv.ps1. */
  queueMutatingDockerOps = false,
}) {
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
  const [pendingCount, setPendingCount] = useState(0);

  const deferMutatingOps = Boolean(integrationMode && queueMutatingDockerOps);
  const compactRuntimeUi = Boolean(integrationMode && integrationCompact);

  useEffect(() => {
    if (integrationMode) return;
    if (carsForSelect.length && !carsForSelect.includes(carId)) {
      setCarId(carsForSelect[0]);
    }
  }, [integrationMode, carsForSelect, carId]);

  const loadHealth = useCallback(async () => {
    try {
      const h = await fetchVehicleRuntimeHealth();
      setHealth(h);
    } catch (e) {
      setHealth({ ok: false, script_found: false, error: e.message });
    }
  }, []);

  const activeCarId = useMemo(() => {
    if (integrationMode) {
      const id = String(integrationCarId || "").trim();
      return id || "car_1";
    }
    return carId;
  }, [integrationMode, integrationCarId, carId]);

  const loadRuntimeState = useCallback(async () => {
    try {
      const r = await fetchVehicleRuntimeState(activeCarId);
      setRuntimeState(r.state ?? null);
      setRuntimeFile(r.file || "");
    } catch (e) {
      setRuntimeState(null);
      setRuntimeFile("");
      setError(e.message || "Failed to load runtime state");
    }
  }, [activeCarId]);

  useEffect(() => {
    loadHealth();
  }, [loadHealth]);

  useEffect(() => {
    setError("");
    if (!compactRuntimeUi) {
      loadRuntimeState();
    }
  }, [loadRuntimeState, compactRuntimeUi]);

  useEffect(() => {
    if (integrationMode) return;
    if (syncPackageFeature == null || String(syncPackageFeature).trim() === "") return;
    setDomain(pkgFeatureToOtaDomain(syncPackageFeature));
  }, [integrationMode, syncPackageFeature]);

  useEffect(() => {
    if (integrationMode) return;
    if (syncPackageVersion == null || String(syncPackageVersion).trim() === "") return;
    setVersion(normalizeOtaDockerVersionArg(syncPackageVersion));
  }, [integrationMode, syncPackageVersion]);

  const effectiveDomain = useMemo(() => {
    if (integrationMode) return pkgFeatureToOtaDomain(integrationFeature || "ADAS");
    return domain;
  }, [integrationMode, integrationFeature, domain]);

  const effectiveVersion = useMemo(() => {
    if (integrationMode) return normalizeOtaDockerVersionArg(integrationVersion ?? "");
    return version;
  }, [integrationMode, integrationVersion, version]);

  const refreshPendingCount = useCallback(async () => {
    if (!deferMutatingOps) return;
    try {
      const r = await fetchVehicleRuntimePendingQueue();
      setPendingCount(Array.isArray(r.queue) ? r.queue.length : 0);
    } catch {
      setPendingCount(0);
    }
  }, [deferMutatingOps]);

  useEffect(() => {
    refreshPendingCount();
  }, [refreshPendingCount]);

  const runOp = async (label, fn) => {
    setBusy(true);
    setError("");
    setLastOut({ stdout: "", stderr: "", exit_code: null, label: "" });
    try {
      const r = await fn();
      let outStdout = r.stdout ?? "";
      if (!outStdout && Array.isArray(r.results)) {
        outStdout = JSON.stringify(
          { results: r.results, pending_remaining: r.pending_remaining, note: r.note },
          null,
          2,
        );
      }
      if (!outStdout && r.queue != null) {
        outStdout = JSON.stringify({ queue_length: r.queue_length, queue: r.queue }, null, 2);
      }
      setLastOut({
        stdout: outStdout,
        stderr: r.stderr || "",
        exit_code: r.exit_code ?? null,
        label,
      });
      if (r.ok === false) {
        const fromResults = Array.isArray(r.results) ? r.results.find((x) => x && x.ok === false) : null;
        setError(
          r.note ||
            r.error ||
            r.stderr?.trim() ||
            fromResults?.stderr?.trim() ||
            (r.exit_code != null ? `Command failed (exit ${r.exit_code})` : "Request returned ok: false"),
        );
      }
      if (!compactRuntimeUi) {
        await loadRuntimeState();
      }
      await refreshPendingCount();
    } catch (e) {
      setError(e.message || String(e));
    } finally {
      setBusy(false);
    }
  };

  const queueBody = (operation) => ({
    operation,
    car_id: activeCarId,
    domain: effectiveDomain,
    version: effectiveVersion,
    sync_shadow: syncShadowOnPromote,
  });

  const onBootstrap = () =>
    deferMutatingOps
      ? runOp("queue-bootstrap", () => postVehicleRuntimeQueueOperation(queueBody("bootstrap")))
      : runOp("bootstrap", () =>
          postVehicleRuntimeBootstrap({ car_id: activeCarId, domain: effectiveDomain, version: effectiveVersion }),
        );

  const onDeployShadow = () =>
    deferMutatingOps
      ? runOp("queue-deploy-shadow", () => postVehicleRuntimeQueueOperation(queueBody("deploy-shadow")))
      : runOp("deploy-shadow", () =>
          postVehicleRuntimeDeployShadow({ car_id: activeCarId, domain: effectiveDomain, version: effectiveVersion }),
        );

  const onPromote = () =>
    deferMutatingOps
      ? runOp("queue-promote", () => postVehicleRuntimeQueueOperation(queueBody("promote")))
      : runOp("promote", () =>
          postVehicleRuntimePromote({
            car_id: activeCarId,
            domain: effectiveDomain,
            version: effectiveVersion,
            sync_shadow: syncShadowOnPromote,
          }),
        );

  const onApplyPending = () => runOp("apply-pending", () => postVehicleRuntimeApplyPending());

  const onDockerStatus = () =>
    runOp("status", () => fetchVehicleRuntimeDockerStatus(activeCarId, effectiveDomain));

  const versionForTag = useMemo(() => normalizeOtaDockerVersionArg(effectiveVersion), [effectiveVersion]);

  const scriptMissing = health && health.script_found === false;

  const runtimeKey = useMemo(() => domainToRuntimeKey(effectiveDomain), [effectiveDomain]);
  const domainRuntime = runtimeState?.domains?.[runtimeKey];
  const primaryVer = domainRuntime?.primary_version;
  const shadowVer = domainRuntime?.shadow_version;
  const lastMsgKo = domainRuntime?.last_message_ko;

  const Root = embed ? "div" : "section";
  const rootClass = embed ? "pkg-docker-nested rollout-campaign-inner" : "card rollout-campaign-card";
  const showStandaloneHeader = !embed || !integrationMode;

  return (
    <Root className={rootClass}>
      {showStandaloneHeader ? (
        <div className={embed ? "pkg-docker-nested-head" : "card-header"}>
          <div>
            {embed ? <h3 className="pkg-docker-nested-title">Docker runtime — Primary / Shadow</h3> : <h2>Docker runtime — Primary / Shadow</h2>}
            <p className="rollout-subtitle subtle">
              Calls <code className="mono">sdv-ota-manager/scripts/ota_vehicle.sh</code> via WSL (same as fleet deploy
              path). Requires Docker in WSL, images like <code className="mono">sdv/adas-feature:v1.0</code>.
            </p>
            <p className="rollout-subtitle subtle" style={{ marginTop: "0.35rem" }}>
              위 Fleet 표의 <strong>A/B 슬롯</strong>은 <code className="mono">deploy/state/*.json</code> + DeploymentMgr
              모델이고, 이 카드의 <strong>버전 숫자</strong>는 Docker 이미지 태그(
              <code className="mono">sdv/adas-feature:v1.0</code> 등)와 별개입니다.
            </p>
            <p className="rollout-subtitle subtle" style={{ marginTop: "0.35rem" }}>
              Manifest 버전이 <code className="mono">v0.3</code> 형태면 스크립트에는 <code className="mono">0.3</code>으로
              넘겨 이미지 <code className="mono">sdv/…-feature:v0.3</code>과 맞춥니다 (중복 <code className="mono">vv</code>{" "}
              방지).
            </p>
          </div>
        </div>
      ) : compactRuntimeUi ? (
        <div className="pkg-docker-nested-head">
          <div className="label pkg-integrated-deploy-label">Docker rollout (same form)</div>
          <p className="rollout-subtitle subtle" style={{ marginTop: "0.25rem" }}>
            <strong>Mutating</strong> steps are <strong>queued</strong> first. They actually run when you click{" "}
            <strong>Apply queued Docker now</strong>, or automatically after a full <code className="mono">run_sdv.ps1</code>{" "}
            restart once status-server is up.
          </p>
          <p className="pkg-integrated-deploy-summary mono subtle">
            car_id={activeCarId} · domain={effectiveDomain} · tag=v{versionForTag || "—"}
            {deferMutatingOps ? ` · pending_queue=${pendingCount}` : null}
          </p>
        </div>
      ) : (
        <div className="pkg-docker-nested-head">
          <div className="label pkg-integrated-deploy-label">Apply to vehicle (Docker Primary / Shadow)</div>
          <p className="rollout-subtitle subtle" style={{ marginTop: "0.25rem" }}>
            Uses <strong>Feature</strong> / <strong>Version</strong> / <strong>Car</strong> from this form. WSL +{" "}
            <code className="mono">sdv-ota-manager/scripts/ota_vehicle.sh</code>. Image example:{" "}
            <code className="mono">sdv/adas-feature:v1.0</code>.
          </p>
          <p className="pkg-integrated-deploy-summary mono subtle">
            car_id={activeCarId} · domain={effectiveDomain} · tag=v{versionForTag || "—"}
          </p>
        </div>
      )}

      {scriptMissing ? (
        <div className="rollout-banner warn">
          <strong>Script not found.</strong> Expected sibling repo{" "}
          <code className="mono">sdv-ota-manager/scripts/ota_vehicle.sh</code> next to{" "}
          <code className="mono">sdv-feature-flag-platform</code>.
        </div>
      ) : null}

      <div className="rollout-flow">
        {!integrationMode ? (
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
              <p className="rollout-hint subtle mono">
                → Docker tag: v{versionForTag || "—"}
              </p>
            </div>
            <p className="rollout-hint subtle">
              Flow: <strong>Bootstrap</strong> (Primary+Shadow same version) → <strong>Deploy shadow</strong> (Shadow
              only) → <strong>Promote</strong> (Primary to target version). Rollback: promote with an older version.
            </p>
          </section>
        ) : compactRuntimeUi ? null : (
          <section className="rollout-block">
            <p className="rollout-hint subtle">
              Flow: <strong>Bootstrap</strong> (Primary+Shadow same version) → <strong>Deploy shadow</strong> (Shadow
              only) → <strong>Promote</strong> (Primary to target version). Rollback: promote with an older version.
            </p>
          </section>
        )}

        <section className="rollout-block">
          <h3 className="rollout-step-title">{integrationMode ? "Commands" : "2. Commands"}</h3>
          <div className="rollout-meta-line subtle mono">
            {formatCmdPreview(activeCarId, effectiveDomain, effectiveVersion, "bootstrap", false)}
          </div>
          <div className="rollout-actions">
            <div className="rollout-action-row">
              <button type="button" className="btn" disabled={busy || scriptMissing} onClick={onBootstrap}>
                {deferMutatingOps ? "Queue bootstrap (primary + shadow)" : "Bootstrap (primary + shadow)"}
              </button>
              <button type="button" className="btn" disabled={busy || scriptMissing} onClick={onDeployShadow}>
                {deferMutatingOps ? "Queue deploy to shadow only" : "Deploy to shadow only"}
              </button>
              <button type="button" className="btn" disabled={busy || scriptMissing} onClick={onPromote}>
                {deferMutatingOps ? "Queue promote primary" : "Promote primary"}
              </button>
              {deferMutatingOps ? (
                <button type="button" className="btn btn-primary" disabled={busy || scriptMissing} onClick={onApplyPending}>
                  Apply queued Docker now
                </button>
              ) : null}
              <button type="button" className="btn btn-secondary" disabled={busy || scriptMissing} onClick={onDockerStatus}>
                Docker status (inspect)
              </button>
              {!compactRuntimeUi ? (
                <button type="button" className="btn btn-secondary" disabled={busy} onClick={loadRuntimeState}>
                  Refresh JSON state
                </button>
              ) : null}
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

        {!compactRuntimeUi ? (
          <>
            <section className="rollout-block">
              <h3 className="rollout-step-title">{integrationMode ? "요약" : "2b. 요약 (선택 도메인)"}</h3>
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
              <h3 className="rollout-step-title">{integrationMode ? "Runtime state file" : "3. Runtime state file"}</h3>
              {runtimeFile ? (
                <p className="rollout-meta-line subtle mono">
                  {runtimeFile}
                </p>
              ) : null}
              <pre className="rollout-cli">{runtimeState ? JSON.stringify(runtimeState, null, 2) : "(no file yet)"}</pre>
            </section>
          </>
        ) : null}

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
    </Root>
  );
}
