import { useEffect, useMemo, useState } from "react";
import mqtt from "mqtt";

const DEFAULT_FEATURES = [
  { label: "Navigation", name: "NavigationFeature" },
  { label: "ADAS", name: "AdasFeature" },
  { label: "EV Energy", name: "EvEnergyFeature" },
];

function nowIso() {
  return new Date().toISOString().slice(11, 19);
}

function normalizeCarId(value) {
  const v = (value || "").trim();
  if (!v) return "car_1";
  return v.startsWith("car_") ? v : `car_${v}`;
}

export default function App() {
  const [brokerWsUrl, setBrokerWsUrl] = useState("ws://127.0.0.1:9001");
  const [carId, setCarId] = useState("car_1");
  const [connected, setConnected] = useState(false);
  const [logLines, setLogLines] = useState([]);

  const topic = useMemo(() => `/sdv/vehicle/${normalizeCarId(carId)}/signals/inbound`, [carId]);

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
      pushLog(`TX topic: ${topic}`);
    });
    client.on("reconnect", () => pushLog("MQTT reconnecting..."));
    client.on("close", () => setConnected(false));
    client.on("error", (err) => pushLog(`MQTT error: ${err?.message || String(err)}`));

    return () => {
      try {
        client.end(true);
      } catch {
      }
    };
    // Recreate client when URL changes.
  }, [brokerWsUrl, topic]);

  const publish = async (payload) => {
    const client = mqtt.connect(brokerWsUrl, {
      clientId: `sdv-test-ui-tx-${Math.random().toString(16).slice(2)}`,
      clean: true,
      keepalive: 10,
      connectTimeout: 4000,
      reconnectPeriod: 0,
    });

    const pushLog = (line) => {
      setLogLines((prev) => {
        const next = [`[${nowIso()}] ${line}`, ...prev];
        return next.slice(0, 200);
      });
    };

    return new Promise((resolve) => {
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
    });
  };

  const onFlagOn = (feature) => publish(`FLAG_ON:${feature}`);
  const onFlagOff = (feature) => publish(`FLAG_OFF:${feature}`);
  const onKill = (feature) => publish(`KILL:${feature}`);

  return (
    <main className="shell">
      <header className="header">
        <div>
          <h1>SDV Test UI</h1>
          <p>MQTT → SignalReceiver → EventBus control demo (FLAG_ON/OFF, KILL).</p>
        </div>
        <div className={`pill ${connected ? "ok" : "bad"}`}>
          {connected ? "connected" : "disconnected"}
        </div>
      </header>

      <section className="card grid">
        <div className="field">
          <label>MQTT WebSocket URL</label>
          <input value={brokerWsUrl} onChange={(e) => setBrokerWsUrl(e.target.value)} placeholder="ws://127.0.0.1:9001" />
          <div className="hint">Mosquitto WebSocket port (see docker-compose: 9001).</div>
        </div>
        <div className="field">
          <label>Car ID</label>
          <input value={carId} onChange={(e) => setCarId(e.target.value)} placeholder="car_1" />
          <div className="hint">Publishes to <span className="mono">{topic}</span></div>
        </div>
      </section>

      <section className="card">
        <h2>Controls</h2>
        <div className="controls">
          {DEFAULT_FEATURES.map((f) => (
            <div key={f.name} className="row">
              <div className="feature">
                <div className="feature-label">{f.label}</div>
                <div className="mono feature-name">{f.name}</div>
                <div className="hint">팀프로젝트는 {f.name} → {f.name}_A/_B 로 자동 확장될 수 있어요.</div>
              </div>
              <div className="buttons">
                <button className="btn on" onClick={() => onFlagOn(f.name)}>FLAG_ON</button>
                <button className="btn off" onClick={() => onFlagOff(f.name)}>FLAG_OFF</button>
                <button className="btn kill" onClick={() => onKill(f.name)}>KILL</button>
              </div>
            </div>
          ))}
        </div>
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

