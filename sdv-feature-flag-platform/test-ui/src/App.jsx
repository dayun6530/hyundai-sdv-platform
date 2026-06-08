import { useCallback, useEffect, useMemo, useState } from "react";
import mqtt from "mqtt";

const DEFAULT_FEATURES = [
  { label: "Navigation", name: "NavigationFeature", stateKey: "Navigation" },
  { label: "ADAS", name: "AdasFeature", stateKey: "ADAS" },
  { label: "EV Energy", name: "EvEnergyFeature", stateKey: "Energy" },
];

const SUB_FEATURES = {
  NavigationFeature: [
    { key: "LOCATION", label: "위치" },
    { key: "ROAD", label: "도로" },
    { key: "GUIDANCE", label: "안내" },
  ],
  AdasFeature: [
    { key: "FCA", label: "FCA" },
    { key: "LKA", label: "LKA" },
    { key: "SCC", label: "SCC" },
  ],
  EvEnergyFeature: [
    { key: "REGEN", label: "회생" },
    { key: "RANGE", label: "주행거리" },
    { key: "ECO", label: "ECO" },
  ],
};

function nowIso() {
  return new Date().toISOString().slice(11, 19);
}

function normalizeCarId(value) {
  const v = (value || "").trim();
  if (!v) return "car_1";
  return v.startsWith("car_") ? v : `car_${v}`;
}

function defaultUiSlice() {
  const sub = {};
  for (const f of DEFAULT_FEATURES) {
    sub[f.name] = { mainOn: true };
    for (const s of SUB_FEATURES[f.name] || []) {
      sub[f.name][s.key] = true;
    }
  }
  return sub;
}

function decodeMqttPayload(msg) {
  if (typeof msg === "string") return msg;
  if (msg instanceof Uint8Array) return new TextDecoder().decode(msg);
  if (msg?.toString) return msg.toString();
  return "";
}

function mergeFlagsState(prev, payload) {
  const id = normalizeCarId(payload?.car_id || "");
  if (!id) return prev;
  const feats = payload?.features;
  if (!feats || typeof feats !== "object") return prev;
  const slice = { ...(prev[id] || defaultUiSlice()) };
  for (const f of DEFAULT_FEATURES) {
    const name = f.name;
    const p = feats[name];
    if (!p || typeof p !== "object") continue;
    slice[name] = { ...(slice[name] || {}) };
    if (typeof p.main === "boolean") slice[name].mainOn = p.main;
    const subs = p.sub || {};
    for (const s of SUB_FEATURES[name] || []) {
      if (typeof subs[s.key] === "boolean") slice[name][s.key] = subs[s.key];
    }
  }
  return { ...prev, [id]: slice };
}

/** Same JSON shape as SignalReceiver / status-server mirror (retained flags/state). */
function buildFlagsStatePayload(carId, slice) {
  const id = normalizeCarId(carId);
  const features = {};
  for (const f of DEFAULT_FEATURES) {
    const name = f.name;
    const row = slice[name] || {};
    const mainOn = !!row.mainOn;
    const subs = {};
    for (const s of SUB_FEATURES[name] || []) {
      subs[s.key] = mainOn ? !!row[s.key] : false;
    }
    features[name] = { main: mainOn, sub: subs };
  }
  return {
    protocol_version: "1.0",
    car_id: id,
    source: "test-ui",
    features,
  };
}

export default function App() {
  const [brokerWsUrl, setBrokerWsUrl] = useState("ws://127.0.0.1:9001");
  const [carId, setCarId] = useState("car_1");
  const [connected, setConnected] = useState(false);
  const [logLines, setLogLines] = useState([]);
  const [uiByCar, setUiByCar] = useState({});

  const topic = useMemo(() => `/sdv/vehicle/${normalizeCarId(carId)}/signals/inbound`, [carId]);
  const stateTopic = useMemo(() => `/sdv/vehicle/${normalizeCarId(carId)}/flags/state`, [carId]);
  const cid = normalizeCarId(carId);

  useEffect(() => {
    setUiByCar((prev) => {
      if (prev[cid]) return prev;
      return { ...prev, [cid]: defaultUiSlice() };
    });
  }, [cid]);

  const ui = uiByCar[cid] ?? defaultUiSlice();

  useEffect(() => {
    const client = mqtt.connect(brokerWsUrl, {
      clientId: `sdv-test-ui-${Math.random().toString(16).slice(2)}`,
      clean: true,
      keepalive: 30,
      connectTimeout: 5000,
      reconnectPeriod: 1500,
    });

    const pushLog = (line) => {
      setLogLines((prev) => {
        const next = [`[${nowIso()}] ${line}`, ...prev];
        return next.slice(0, 200);
      });
    };

    client.on("connect", () => {
      setConnected(true);
      pushLog(`MQTT connected: ${brokerWsUrl}`);
      pushLog(`TX ${topic} | STATE ${stateTopic}`);
      client.subscribe(topic, { qos: 1 }, (err) => {
        if (err) pushLog(`subscribe inbound error: ${err?.message || String(err)}`);
      });
      client.subscribe(stateTopic, { qos: 1 }, (err) => {
        if (err) pushLog(`subscribe state error: ${err?.message || String(err)}`);
      });
    });
    client.on("reconnect", () => pushLog("MQTT reconnecting..."));
    client.on("close", () => setConnected(false));
    client.on("error", (err) => pushLog(`MQTT error: ${err?.message || String(err)}`));
    client.on("message", (t, msg) => {
      const topicStr = t.toString();
      if (!topicStr.endsWith("/flags/state")) return;
      try {
        const text = decodeMqttPayload(msg);
        const j = JSON.parse(text);
        setUiByCar((prev) => mergeFlagsState(prev, j));
        pushLog(`RX state ← ${topicStr}`);
      } catch (e) {
        pushLog(`RX state parse error: ${e?.message || String(e)}`);
      }
    });

    return () => {
      try {
        client.end(true);
      } catch {
      }
    };
  }, [brokerWsUrl, topic, stateTopic]);

  const pushLog = useCallback((line) => {
    setLogLines((prev) => {
      const next = [`[${nowIso()}] ${line}`, ...prev];
      return next.slice(0, 200);
    });
  }, []);

  const publish = useCallback(
    (payload) =>
      new Promise((resolve) => {
        const client = mqtt.connect(brokerWsUrl, {
          clientId: `sdv-test-ui-tx-${Math.random().toString(16).slice(2)}`,
          clean: true,
          keepalive: 10,
          connectTimeout: 4000,
          reconnectPeriod: 0,
        });

        client.on("connect", () => {
          client.publish(topic, payload, { qos: 1 }, () => {
            pushLog(`TX ${topic} ← ${payload}`);
            client.end(true);
            resolve(true);
          });
        });
        client.on("error", (err) => {
          pushLog(`TX error: ${err?.message || String(err)}`);
          try {
            client.end(true);
          } catch {
          }
          resolve(false);
        });
      }),
    [brokerWsUrl, topic, pushLog],
  );

  /** Retained flags/state so Dashboard + status-server see OFF even when car_N is stopped. */
  const publishFlagsState = useCallback(
    (id, slice) =>
      new Promise((resolve) => {
        const stTopic = `/sdv/vehicle/${normalizeCarId(id)}/flags/state`;
        const body = JSON.stringify(buildFlagsStatePayload(id, slice));
        const client = mqtt.connect(brokerWsUrl, {
          clientId: `sdv-test-ui-st-${Math.random().toString(16).slice(2)}`,
          clean: false,
          keepalive: 10,
          connectTimeout: 4000,
          reconnectPeriod: 0,
        });
        client.on("connect", () => {
          client.publish(stTopic, body, { qos: 1, retain: true }, () => {
            pushLog(`TX ${stTopic} ← retain (${body.length}B)`);
            client.end(true);
            resolve(true);
          });
        });
        client.on("error", (err) => {
          pushLog(`flags/state TX error: ${err?.message || String(err)}`);
          try {
            client.end(true);
          } catch {
          }
          resolve(false);
        });
      }),
    [brokerWsUrl, pushLog],
  );

  const applyUiSlice = useCallback(
    (id, slice) => {
      setUiByCar((prev) => ({ ...prev, [normalizeCarId(id)]: slice }));
    },
    [],
  );

  const sendMain = async (feature, on) => {
    const id = normalizeCarId(carId);
    const next = { ...(uiByCar[id] || ui) };
    next[feature] = { ...(next[feature] || {}), mainOn: on };
    if (!on) {
      for (const s of SUB_FEATURES[feature] || []) {
        next[feature][s.key] = false;
      }
    }
    applyUiSlice(id, next);
    const payload = on ? `FLAG_ON:${feature}` : `FLAG_OFF:${feature}`;
    await publish(payload);
    await publishFlagsState(id, next);
  };

  const sendSub = async (feature, subKey, on) => {
    const id = normalizeCarId(carId);
    const next = { ...(uiByCar[id] || ui) };
    next[feature] = { ...(next[feature] || {}), [subKey]: on };
    if (on) {
      next[feature].mainOn = true;
    }
    applyUiSlice(id, next);
    const payload = on ? `FLAG_ON:${feature}.${subKey}` : `FLAG_OFF:${feature}.${subKey}`;
    await publish(payload);
    await publishFlagsState(id, next);
  };

  const statusApiBase = import.meta.env.VITE_STATUS_API || "";
  const [killSyncLog, setKillSyncLog] = useState([]);

  const onKill = async (feature) => {
    const id = normalizeCarId(carId);
    const meta = DEFAULT_FEATURES.find((f) => f.name === feature);
    const stateKey = meta?.stateKey || feature;
    const next = { ...(uiByCar[id] || ui) };
    next[feature] = { ...(next[feature] || {}), mainOn: false };
    for (const s of SUB_FEATURES[feature] || []) {
      next[feature][s.key] = false;
    }
    applyUiSlice(id, next);
    const runtimePayload = `KILL:${feature}`;
    await publish(runtimePayload);
    // Retained flags/state OFF (separate from deploy/state kill_switch — runtime mirror only).
    await publishFlagsState(id, next);

    const logLines = [`Runtime KILL sent: ${runtimePayload}`];
    try {
      const res = await fetch(`${statusApiBase}/api/fleet/kill-switch`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
          car_id: id,
          feature: stateKey,
          runtime_feature: feature,
          kill: true,
          reason: "MQTT TEST runtime kill",
          source: "mqtt-test-ui",
          send_runtime: false,
          apply_fleet_kill_policy: false,
        }),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || data.ok === false) {
        logLines.push(`Deploy state sync FAILED: ${data.error || res.statusText}`);
      } else {
        logLines.push(`Deploy state synced: ${stateKey} KILL ${data.kill_switch ? "ON" : "OFF"}`);
        logLines.push(`History written: ${data.history_written ? "success" : "unknown"}`);
        if (data.runtime_dispatched) {
          logLines.push("Runtime dispatch (server): sent");
        }
      }
    } catch (e) {
      logLines.push(`Deploy state sync FAILED: ${e.message}`);
    }
    setKillSyncLog((prev) => [{ ts: nowIso(), feature, lines: logLines }, ...prev].slice(0, 12));
  };

  const onKillAll = async () => {
    for (const f of DEFAULT_FEATURES) {
      await onKill(f.name);
    }
  };

  const [collectorBatch, setCollectorBatch] = useState(50);
  const [collectorFlush, setCollectorFlush] = useState(1000);
  const [publishDefault, setPublishDefault] = useState(2000);
  const [publishAdas, setPublishAdas] = useState(2000);
  const [publishNav, setPublishNav] = useState(1000);
  const [publishEv, setPublishEv] = useState(2000);
  const [collectorStatus, setCollectorStatus] = useState("");

  const loadCollectorConfig = useCallback(async () => {
    const id = normalizeCarId(carId);
    try {
      const res = await fetch(`${statusApiBase}/api/telemetry/collector-config/${id}`);
      const data = await res.json();
      if (!res.ok || !data.ok) throw new Error(data.error || res.statusText);
      setCollectorBatch(data.batch_max);
      setCollectorFlush(data.flush_ms);
      setPublishDefault(data.publish_default_ms ?? 1000);
      const fp = data.feature_publish_ms || {};
      setPublishAdas(fp.AdasFeature ?? data.publish_default_ms ?? 1000);
      setPublishNav(fp.NavigationFeature ?? data.publish_default_ms ?? 1000);
      setPublishEv(fp.EvEnergyFeature ?? data.publish_default_ms ?? 1000);
      setCollectorStatus(`loaded (${data.runtime_source || "?"} · ${data.runtime_path || ""})`);
    } catch (e) {
      setCollectorStatus(`load failed: ${e.message}`);
    }
  }, [carId, statusApiBase]);

  const saveCollectorConfig = useCallback(async () => {
    const id = normalizeCarId(carId);
    try {
      const res = await fetch(`${statusApiBase}/api/telemetry/collector-config/${id}`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          batch_max: collectorBatch,
          flush_ms: collectorFlush,
          publish_default_ms: publishDefault,
          feature_publish_ms: {
            AdasFeature: publishAdas,
            NavigationFeature: publishNav,
            EvEnergyFeature: publishEv,
          },
        }),
      });
      const data = await res.json();
      if (!res.ok || !data.ok) throw new Error(data.error || res.statusText);
      setCollectorStatus(data.message || "saved (no car restart)");
    } catch (e) {
      setCollectorStatus(`save failed: ${e.message}`);
    }
  }, [
    carId,
    collectorBatch,
    collectorFlush,
    publishDefault,
    publishAdas,
    publishNav,
    publishEv,
    statusApiBase,
  ]);

  useEffect(() => {
    loadCollectorConfig();
  }, [loadCollectorConfig]);

  return (
    <main className="shell">
      <header className="header">
        <div>
          <h1>SDV Test UI</h1>
          <p>MQTT 제어 + <code className="mono">flags/state</code> 로 버튼이 실제 반영 상태와 동기화됩니다.</p>
        </div>
        <div className={`pill ${connected ? "ok" : "bad"}`}>
          {connected ? "connected" : "disconnected"}
        </div>
      </header>

      <section className="card grid">
        <div className="field">
          <label>MQTT WebSocket URL</label>
          <input value={brokerWsUrl} onChange={(e) => setBrokerWsUrl(e.target.value)} placeholder="ws://127.0.0.1:9001" />
          <div className="hint">Mosquitto WebSocket (docker-compose: 9001).</div>
        </div>
        <div className="field">
          <label>Car ID</label>
          <input
            value={carId}
            onChange={(e) => setCarId(e.target.value)}
            onBlur={loadCollectorConfig}
            placeholder="car_1"
          />
          <div className="hint">
            제어: <span className="mono">{topic}</span>
            <br />
            상태 구독: <span className="mono">{stateTopic}</span> (SignalReceiver가 retain 발행)
          </div>
        </div>
      </section>

      <section className="card">
        <h2>메인 Feature</h2>
        <div className="controls">
          {DEFAULT_FEATURES.map((f) => (
            <div key={f.name} className="row">
              <div className="feature">
                <div className="feature-label">{f.label}</div>
                <div className="mono feature-name">{f.name}</div>
              </div>
              <div className="buttons">
                <button
                  type="button"
                  className={`btn on${ui[f.name]?.mainOn ? " active" : ""}`}
                  onClick={() => sendMain(f.name, true)}
                >
                  FLAG_ON
                </button>
                <button
                  type="button"
                  className={`btn off${!ui[f.name]?.mainOn ? " active" : ""}`}
                  onClick={() => sendMain(f.name, false)}
                >
                  FLAG_OFF
                </button>
                <button type="button" className="btn kill" onClick={() => onKill(f.name)}>
                  KILL
                </button>
              </div>
            </div>
          ))}
        </div>
        <div className="buttons" style={{ marginTop: 12 }}>
          <button type="button" className="btn kill" onClick={onKillAll}>
            KILL ALL (runtime + deploy state per feature)
          </button>
        </div>
        {killSyncLog.length ? (
          <div className="kill-sync-log" style={{ marginTop: 12 }}>
            <div className="hint">Integrated Kill Switch sync (MQTT TEST → status-server)</div>
            <ul className="mono tiny">
              {killSyncLog.map((entry, idx) => (
                <li key={`${entry.ts}-${entry.feature}-${idx}`}>
                  [{entry.ts}] {entry.feature}: {entry.lines.join(" · ")}
                </li>
              ))}
            </ul>
          </div>
        ) : null}
      </section>

      <section className="card">
        <h2>서브 기능 (FLAG_ON/OFF:&lt;Feature&gt;.&lt;KEY&gt;)</h2>
        <div className="controls">
          {DEFAULT_FEATURES.map((f) => (
            <div key={`sub-${f.name}`} className="sub-block">
              <div className="sub-title">{f.label}</div>
              <div className="sub-grid">
                {(SUB_FEATURES[f.name] || []).map((s) => (
                  <div key={s.key} className="sub-row">
                    <span className="mono sub-key">{s.key}</span>
                    <div className="buttons compact">
                      <button
                        type="button"
                        className={`btn on sm${ui[f.name]?.[s.key] ? " active" : ""}`}
                        onClick={() => sendSub(f.name, s.key, true)}
                      >
                        ON
                      </button>
                      <button
                        type="button"
                        className={`btn off sm${!ui[f.name]?.[s.key] ? " active" : ""}`}
                        onClick={() => sendSub(f.name, s.key, false)}
                      >
                        OFF
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            </div>
          ))}
        </div>
      </section>

      <section className="card">
        <h2>Telemetry 설정 (JSON)</h2>
        <p className="hint">
          주기·배치는 <span className="mono">config/telemetry/runtime/&lt;car&gt;.json</span> 만 사용합니다. UI 저장 시
          해당 JSON이 갱신되며 Feature·DataCollector가 car 재시작 없이 반영합니다.
        </p>
        <h3 style={{ marginTop: 12, fontSize: "0.95rem" }}>② DataCollector → DB/Grafana</h3>
        <div className="grid">
          <div className="field">
            <label>batch_max (1–100건)</label>
            <input
              type="number"
              min={1}
              max={100}
              value={collectorBatch}
              onChange={(e) => setCollectorBatch(Number(e.target.value))}
            />
          </div>
          <div className="field">
            <label>flush_ms (500–2000ms)</label>
            <input
              type="number"
              min={500}
              max={2000}
              step={100}
              value={collectorFlush}
              onChange={(e) => setCollectorFlush(Number(e.target.value))}
            />
          </div>
        </div>
        <h3 style={{ marginTop: 16, fontSize: "0.95rem" }}>① Feature → EventBus 발행 주기</h3>
        <div className="grid">
          <div className="field">
            <label>기본 publish_ms (200–30000)</label>
            <input
              type="number"
              min={200}
              max={30000}
              step={100}
              value={publishDefault}
              onChange={(e) => setPublishDefault(Number(e.target.value))}
            />
          </div>
          <div className="field">
            <label>AdasFeature ms</label>
            <input
              type="number"
              min={200}
              max={30000}
              step={100}
              value={publishAdas}
              onChange={(e) => setPublishAdas(Number(e.target.value))}
            />
          </div>
          <div className="field">
            <label>NavigationFeature ms</label>
            <input
              type="number"
              min={200}
              max={30000}
              step={100}
              value={publishNav}
              onChange={(e) => setPublishNav(Number(e.target.value))}
            />
          </div>
          <div className="field">
            <label>EvEnergyFeature ms</label>
            <input
              type="number"
              min={200}
              max={30000}
              step={100}
              value={publishEv}
              onChange={(e) => setPublishEv(Number(e.target.value))}
            />
          </div>
        </div>
        <div className="buttons" style={{ marginTop: 12 }}>
          <button type="button" className="btn on" onClick={saveCollectorConfig}>
            JSON 저장 · 즉시 반영
          </button>
          <button type="button" className="btn off sm" onClick={loadCollectorConfig}>
            다시 읽기
          </button>
        </div>
        {collectorStatus ? <p className="hint">{collectorStatus}</p> : null}
      </section>

      <section className="card">
        <h2>Log</h2>
        <div className="log">
          {logLines.length ? logLines.map((l, i) => <div key={i} className="mono">{l}</div>) : <div className="hint">No events yet.</div>}
        </div>
      </section>
    </main>
  );
}
