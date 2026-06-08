import { useCallback, useEffect, useMemo, useState } from "react";
import {
  fetchVehicleRuntimeDockerStatus,
  fetchVehicleRuntimeHealth,
  fetchVehicleRuntimePendingQueue,
  fetchVehicleRuntimeState,
  postVehicleRuntimeApplyPending,
  postVehicleRuntimeClearPendingQueue,
  postVehicleRuntimeBootstrap,
  postVehicleRuntimeDeployShadow,
  postVehicleRuntimePromote,
  postVehicleRuntimeQueueOperation,
} from "../api/vehicleRuntime.js";
import { normalizeOtaDockerVersionArg, pkgFeatureToOtaDomain } from "../dockerVersionUtils.js";
import { carIdOptions } from "./rolloutModel.js";
import { requestPygamePromoteConfirm } from "../api/pygameBridge.js";

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

  const [health, setHealth] = useState(null);
  const [runtimeState, setRuntimeState] = useState(null);
  const [runtimeFile, setRuntimeFile] = useState("");

  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [lastOut, setLastOut] = useState({ stdout: "", stderr: "", exit_code: null, label: "" });
  const [pendingCount, setPendingCount] = useState(0);
  const [pendingQueueItems, setPendingQueueItems] = useState([]);
  const [promoteAwaitingPygame, setPromoteAwaitingPygame] = useState(false);

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
    // Always load so Shadow lane buttons work in package (compact) integration too.
    loadRuntimeState();
  }, [loadRuntimeState]);

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

  const refreshPendingQueue = useCallback(async () => {
    if (!deferMutatingOps) {
      setPendingCount(0);
      setPendingQueueItems([]);
      return;
    }
    try {
      const r = await fetchVehicleRuntimePendingQueue();
      const q = Array.isArray(r.queue) ? r.queue : [];
      setPendingQueueItems(q);
      setPendingCount(q.length);
    } catch {
      setPendingCount(0);
      setPendingQueueItems([]);
    }
  }, [deferMutatingOps]);

  useEffect(() => {
    refreshPendingQueue();
  }, [refreshPendingQueue]);

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
        const scriptErr = (fromResults?.stderr || r.stderr || "").trim();
        setError(
          scriptErr
            ? `${scriptErr}${r.note ? `\n\n— ${r.note}` : ""}`
            : r.note ||
                r.error ||
                (r.exit_code != null ? `Command failed (exit ${r.exit_code})` : "Request returned ok: false"),
        );
      }
      if (!compactRuntimeUi) {
        await loadRuntimeState();
      }
      await refreshPendingQueue();
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
    sync_shadow: false,
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

  const promoteFeatureLabel = useMemo(() => {
    if (integrationFeature) {
      if (integrationFeature === "Energy") return "EV Energy";
      return integrationFeature;
    }
    if (effectiveDomain === "EvEnergy") return "EV Energy";
    if (effectiveDomain === "Navigation") return "Navigation";
    return "ADAS";
  }, [integrationFeature, effectiveDomain]);

  const executePromote = () => {
    if (deferMutatingOps) {
      runOp("queue-promote", () => postVehicleRuntimeQueueOperation(queueBody("promote")));
    } else {
      runOp("promote", () =>
        postVehicleRuntimePromote({
          car_id: activeCarId,
          domain: effectiveDomain,
          version: effectiveVersion,
          sync_shadow: false,
        }),
      );
    }
  };

  const onPromote = async () => {
    if (busy || promoteAwaitingPygame) return;
    setPromoteAwaitingPygame(true);
    try {
      const accepted = await requestPygamePromoteConfirm({
        feature: promoteFeatureLabel,
        version: effectiveVersion ? `v${versionForTag || effectiveVersion}` : "",
        message: `${promoteFeatureLabel} feature의 새 버전을 Primary로 업데이트하시겠습니까?`,
        detail: `차량: ${activeCarId}\n버전: v${effectiveVersion || "—"}`,
      });
      if (accepted) executePromote();
    } catch (e) {
      setError(e?.message || String(e));
    } finally {
      setPromoteAwaitingPygame(false);
    }
  };

  const onApplyPending = () => runOp("apply-pending", () => postVehicleRuntimeApplyPending());

  const onClearPendingQueueAll = () =>
    runOp("clear-pending-queue(all)", () => postVehicleRuntimeClearPendingQueue("all"));

  const onDropPendingQueueHead = () =>
    runOp("clear-pending-queue(head)", () => postVehicleRuntimeClearPendingQueue("head"));

  const onDockerStatus = () =>
    runOp("status", () => fetchVehicleRuntimeDockerStatus(activeCarId, effectiveDomain));

  const versionForTag = useMemo(() => normalizeOtaDockerVersionArg(effectiveVersion), [effectiveVersion]);

  const scriptMissing = health && health.script_found === false;

  const runtimeKey = useMemo(() => domainToRuntimeKey(effectiveDomain), [effectiveDomain]);
  const domainRuntime = runtimeState?.domains?.[runtimeKey];
  const primaryVer = domainRuntime?.primary_version;
  const shadowVer = domainRuntime?.shadow_version;
  const shadowLaneEnabled = Boolean(domainRuntime?.shadow_enabled);
  const lastMsgKo = domainRuntime?.last_message_ko;

  const shadowLaneVersionsDiffer = useMemo(() => {
    if (!shadowLaneEnabled) return false;
    const p = normalizeOtaDockerVersionArg(primaryVer);
    const s = normalizeOtaDockerVersionArg(shadowVer);
    return Boolean(p && s && p !== s);
  }, [primaryVer, shadowVer, shadowLaneEnabled]);

  const primaryVersionForApi = useMemo(
    () => (primaryVer != null && String(primaryVer).trim() !== "" ? normalizeOtaDockerVersionArg(String(primaryVer)) : ""),
    [primaryVer],
  );
  const shadowVersionForApi = useMemo(
    () => (shadowVer != null && String(shadowVer).trim() !== "" ? normalizeOtaDockerVersionArg(String(shadowVer)) : ""),
    [shadowVer],
  );

  const onApplyShadowToPrimary = () => {
    if (!shadowLaneVersionsDiffer || !shadowVersionForApi) return undefined;
    const body = {
      operation: "promote",
      car_id: activeCarId,
      domain: effectiveDomain,
      version: shadowVersionForApi,
      sync_shadow: true,
    };
    return deferMutatingOps
      ? runOp("queue-promote-shadow→primary", () => postVehicleRuntimeQueueOperation(body))
      : runOp("promote-shadow→primary", () =>
          postVehicleRuntimePromote({
            car_id: activeCarId,
            domain: effectiveDomain,
            version: shadowVersionForApi,
            sync_shadow: true,
          }),
        );
  };

  const onDropShadowCandidate = () => {
    if (!primaryVersionForApi) return undefined;
    const body = {
      operation: "deploy-shadow",
      car_id: activeCarId,
      domain: effectiveDomain,
      version: primaryVersionForApi,
    };
    return deferMutatingOps
      ? runOp("queue-drop-shadow", () => postVehicleRuntimeQueueOperation(body))
      : runOp("drop-shadow", () =>
          postVehicleRuntimeDeployShadow({
            car_id: activeCarId,
            domain: effectiveDomain,
            version: primaryVersionForApi,
          }),
        );
  };

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
              위 Fleet 표의 <strong>Primary/Candidate 역할</strong>은 <code className="mono">deploy/state/*.json</code> + DeploymentMgr
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
          <div className="label pkg-integrated-deploy-label">Docker rollout</div>
          <p className="pkg-integrated-deploy-summary mono subtle">
            car_id={activeCarId} · domain={effectiveDomain} · tag=v{versionForTag || "—"}
            {deferMutatingOps ? ` · pending_queue=${pendingCount}` : null}
          </p>
        </div>
      ) : (
        <div className="pkg-docker-nested-head">
          <div className="label pkg-integrated-deploy-label">Docker rollout</div>
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

      {deferMutatingOps && pendingCount > 0 ? (
        <div className="rollout-banner warn" style={{ margin: "0.5rem 0" }}>
          <strong>큐에 Docker 작업이 {pendingCount}건 대기 중입니다.</strong>{" "}
          <code className="mono">Queue …</code> 버튼은 파일 큐에만 넣을 뿐이며, 아래{" "}
          <strong>Apply queued Docker now</strong>를 눌러야 <code className="mono">ota_vehicle.sh</code>가 실제로
          실행되고 <code className="mono">vehicle_runtime</code> JSON이 갱신됩니다.
          <div className="rollout-action-row" style={{ marginTop: "0.6rem", flexWrap: "wrap" }}>
            <button
              type="button"
              className="btn btn-secondary"
              disabled={busy}
              onClick={() => void refreshPendingQueue()}
            >
              Refresh queue list
            </button>
            <button
              type="button"
              className="btn btn-secondary"
              disabled={busy || pendingCount === 0}
              onClick={() => void onDropPendingQueueHead()}
            >
              Remove first item only
            </button>
            <button
              type="button"
              className="btn"
              style={{ borderColor: "var(--danger, #c0392b)", color: "var(--danger, #c0392b)" }}
              disabled={busy || pendingCount === 0}
              onClick={() => void onClearPendingQueueAll()}
            >
              Clear entire queue
            </button>
          </div>
          {pendingQueueItems.length ? (
            <pre className="rollout-cli" style={{ marginTop: "0.45rem", maxHeight: "12rem", overflow: "auto" }}>
              {JSON.stringify(pendingQueueItems, null, 2)}
            </pre>
          ) : null}
        </div>
      ) : deferMutatingOps ? (
        <div className="rollout-hint subtle" style={{ margin: "0.35rem 0" }}>
          큐가 비어 있습니다.{" "}
          <button type="button" className="btn btn-secondary" disabled={busy} onClick={() => void refreshPendingQueue()}>
            Refresh queue
          </button>
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
              <button
                type="button"
                className="btn"
                disabled={busy || promoteAwaitingPygame || scriptMissing}
                onClick={onPromote}
              >
                {promoteAwaitingPygame
                  ? "pygame 확인 대기…"
                  : deferMutatingOps
                    ? "Queue promote primary"
                    : "Promote primary"}
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

            <div className="rollout-shadow-lane-compact" style={{ marginTop: "0.75rem" }}>
              <p className="rollout-meta-line mono subtle" style={{ marginBottom: "0.35rem" }}>
                runtime: Primary v{primaryVersionForApi || "—"} · Shadow v{shadowVersionForApi || "—"}
                {compactRuntimeUi ? (
                  <button
                    type="button"
                    className="btn btn-secondary"
                    style={{ marginLeft: "0.5rem", padding: "0.2rem 0.5rem", fontSize: "0.85rem" }}
                    disabled={busy}
                    onClick={() => loadRuntimeState()}
                  >
                    Refresh runtime
                  </button>
                ) : null}
              </p>
              <div className="rollout-action-row">
                <button
                  type="button"
                  className="btn btn-primary"
                  disabled={busy || scriptMissing || !shadowLaneVersionsDiffer || !shadowVersionForApi}
                  title={
                    !shadowLaneVersionsDiffer
                      ? "Primary와 Shadow 버전이 같으면 Shadow 후보가 없습니다."
                      : "promote shadow 버전 + --sync-shadow"
                  }
                  onClick={() => void onApplyShadowToPrimary()}
                >
                  {deferMutatingOps ? "Queue Shadow→Primary (sync)" : "Apply: Shadow → Primary"}
                </button>
                <button
                  type="button"
                  className="btn btn-secondary"
                  disabled={busy || scriptMissing || !primaryVersionForApi}
                  title={
                    primaryVersionForApi
                      ? "deploy-shadow로 Shadow만 Primary와 같은 이미지로 (후보 폐기)"
                      : "vehicle_runtime JSON에 primary_version이 없습니다. Refresh runtime."
                  }
                  onClick={() => void onDropShadowCandidate()}
                >
                  {deferMutatingOps ? "Queue drop shadow (discard)" : "Drop: Shadow → match Primary"}
                </button>
              </div>
            </div>
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
              <p className="rollout-hint subtle" style={{ marginTop: "0.5rem" }}>
                Shadow 승격/폐기 버튼은 위 <strong>Commands</strong> 블록에 있습니다.
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
