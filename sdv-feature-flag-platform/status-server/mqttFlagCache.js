/**
 * Mirrors test-ui FLAG_ON/OFF into fleet HTTP APIs (same topics as SignalReceiver).
 * Subscribes to:
 *   - /sdv/vehicle/car_N/flags/state (retained snapshot from C++ when car is running)
 *   - /sdv/vehicle/car_N/signals/inbound (test-ui TX — works even when car_N is stopped)
 */
const mqtt = require("mqtt");

const LOGICAL_TO_DEPLOY = {
  AdasFeature: "ADAS",
  NavigationFeature: "Navigation",
  EvEnergyFeature: "Energy",
};

const LOGICAL_FEATURES = Object.keys(LOGICAL_TO_DEPLOY);
const SUB_KEYS = {
  AdasFeature: ["FCA", "LKA", "SCC"],
  NavigationFeature: ["LOCATION", "ROAD", "GUIDANCE"],
  EvEnergyFeature: ["REGEN", "RANGE", "ECO"],
};

const CAR_ID_RE = /^car_(10|[1-9])$/;
const FLAGS_TOPIC_RE = /\/sdv\/vehicle\/(car_(?:10|[1-9]))\/flags\/state$/;
const INBOUND_TOPIC_RE = /\/sdv\/vehicle\/(car_(?:10|[1-9]))\/signals\/inbound$/;

function trim(s) {
  return String(s || "").trim();
}

function toLogicalFeatureName(name) {
  const n = trim(name);
  if (n.endsWith("_A") || n.endsWith("_B")) {
    return n.slice(0, -2);
  }
  return n;
}

function logicalBaseFromFlagRest(rest) {
  const t = trim(rest);
  const dot = t.indexOf(".");
  return dot === -1 ? t : t.slice(0, dot);
}

/** Per-car mirror matching SignalReceiver LogicalFlagSnapshot. */
class CarFlagMirror {
  constructor(carId) {
    this.carId = carId;
    /** @type {Map<string, boolean>} */
    this.mainOn = new Map();
    /** @type {Map<string, Map<string, boolean>>} */
    this.subVal = new Map();
    /** @type {Set<string>} */
    this.killedLogical = new Set();
  }

  main(logical) {
    if (this.killedLogical.has(logical)) {
      return false;
    }
    if (!this.mainOn.has(logical)) {
      return true;
    }
    return this.mainOn.get(logical);
  }

  sub(logical, key) {
    if (!this.main(logical)) {
      return false;
    }
    const m = this.subVal.get(logical);
    if (!m || !m.has(key)) {
      return true;
    }
    return m.get(key);
  }

  setMain(logical, on) {
    if (on) {
      this.mainOn.set(logical, true);
      this.subVal.delete(logical);
      this.killedLogical.delete(logical);
    } else {
      this.mainOn.set(logical, false);
    }
  }

  setSub(logical, key, on) {
    if (on) {
      const m = this.subVal.get(logical);
      if (m) {
        m.delete(key);
        if (m.size === 0) {
          this.subVal.delete(logical);
        }
      }
    } else {
      if (!this.subVal.has(logical)) {
        this.subVal.set(logical, new Map());
      }
      this.subVal.get(logical).set(key, false);
    }
  }

  applyFlagOnRest(rest) {
    const t = trim(rest);
    const dot = t.indexOf(".");
    if (dot === -1) {
      this.setMain(toLogicalFeatureName(t), true);
      return;
    }
    const base = toLogicalFeatureName(t.slice(0, dot));
    const key = trim(t.slice(dot + 1));
    if (base && key) {
      this.setSub(base, key, true);
    }
  }

  applyFlagOffRest(rest) {
    const t = trim(rest);
    const dot = t.indexOf(".");
    if (dot === -1) {
      this.setMain(toLogicalFeatureName(t), false);
      return;
    }
    const base = toLogicalFeatureName(t.slice(0, dot));
    const key = trim(t.slice(dot + 1));
    if (base && key) {
      this.setSub(base, key, false);
    }
  }

  applyKillLogical(logical) {
    const log = trim(logical);
    if (!log) {
      return;
    }
    this.setMain(log, false);
    this.killedLogical.add(log);
  }

  loadFromStatePayload(payload) {
    const feats = payload?.features;
    if (!feats || typeof feats !== "object") {
      return;
    }
    for (const logical of LOGICAL_FEATURES) {
      const f = feats[logical];
      if (!f || typeof f !== "object") {
        continue;
      }
      if (typeof f.main === "boolean") {
        this.mainOn.set(logical, f.main);
        if (f.main) {
          this.killedLogical.delete(logical);
        }
      }
      const subs = f.sub || {};
      if (subs && typeof subs === "object") {
        const m = new Map();
        for (const [k, v] of Object.entries(subs)) {
          if (typeof v === "boolean") {
            m.set(k, v);
          }
        }
        if (m.size > 0) {
          this.subVal.set(logical, m);
        }
      }
    }
  }

  toStatePayload() {
    const features = {};
    for (const logical of LOGICAL_FEATURES) {
      const mainOn = this.main(logical);
      const subs = {};
      for (const k of SUB_KEYS[logical] || []) {
        subs[k] = mainOn ? this.sub(logical, k) : false;
      }
      features[logical] = { main: mainOn, sub: subs };
    }
    return {
      protocol_version: "1.0",
      car_id: this.carId,
      source: "status-server-mirror",
      features,
      received_at: new Date().toISOString(),
    };
  }
}

class MqttFlagCache {
  constructor() {
    /** @type {Map<string, CarFlagMirror>} */
    this.mirrors = new Map();
    this.connected = false;
    this.lastError = "";
    this.client = null;
  }

  mirrorFor(carId) {
    if (!this.mirrors.has(carId)) {
      this.mirrors.set(carId, new CarFlagMirror(carId));
    }
    return this.mirrors.get(carId);
  }

  start(brokerUrl) {
    if (this.client) {
      return;
    }
    const url = String(brokerUrl || "mqtt://127.0.0.1:1884").trim();
    this.client = mqtt.connect(url, {
      clientId: `sdv-status-flags-${Math.random().toString(16).slice(2, 10)}`,
      clean: false,
      reconnectPeriod: 2000,
      connectTimeout: 8000,
      keepalive: 30,
    });

    this.client.on("connect", () => {
      this.connected = true;
      this.lastError = "";
      this.client.subscribe(
        ["/sdv/vehicle/+/flags/state", "/sdv/vehicle/+/signals/inbound"],
        { qos: 1 },
        (err) => {
          if (err) {
            this.lastError = err.message || String(err);
          }
        },
      );
    });

    this.client.on("reconnect", () => {
      this.connected = false;
    });
    this.client.on("error", (err) => {
      this.lastError = err?.message || String(err);
    });
    this.client.on("close", () => {
      this.connected = false;
    });

    this.client.on("message", (topic, buf) => {
      try {
        const topicStr = topic.toString();
        const text = buf.toString("utf8").trim();
        if (!text) {
          return;
        }

        const flagsM = topicStr.match(FLAGS_TOPIC_RE);
        if (flagsM) {
          const payload = JSON.parse(text);
          const carId = String(payload?.car_id || flagsM[1] || "").trim();
          if (!CAR_ID_RE.test(carId)) {
            return;
          }
          const mirror = this.mirrorFor(carId);
          mirror.loadFromStatePayload(payload);
          return;
        }

        const inboundM = topicStr.match(INBOUND_TOPIC_RE);
        if (inboundM) {
          const carId = inboundM[1];
          this.applyInboundLine(carId, text);
        }
      } catch (e) {
        this.lastError = e?.message || String(e);
      }
    });
  }

  publishMirrorState(carId) {
    if (!this.client || !this.connected || !CAR_ID_RE.test(carId)) {
      return;
    }
    const mirror = this.mirrors.get(carId);
    if (!mirror) {
      return;
    }
    const topic = `/sdv/vehicle/${carId}/flags/state`;
    const body = JSON.stringify(mirror.toStatePayload());
    this.client.publish(topic, body, { qos: 1, retain: true });
  }

  /**
   * Publish runtime kill/inbound signal (same topic as test-ui / SignalReceiver).
   * Also updates local mirror so status-server fleet view stays consistent.
   */
  publishInboundSignal(carId, line, options = {}) {
    const topicPrefix = String(options.topicPrefix || "/sdv/vehicle").replace(/\/$/, "");
    const topic = `${topicPrefix}/${carId}/signals/inbound`;
    const payload = String(line || "").trim();
    if (!CAR_ID_RE.test(carId) || !payload) {
      return { dispatched: false, topic, payload, error: "invalid car_id or payload" };
    }
    if (!this.client || !this.connected) {
      return {
        dispatched: false,
        topic,
        payload,
        error: this.lastError || "MQTT not connected",
      };
    }
    try {
      this.client.publish(topic, payload, { qos: 1 });
      this.applyInboundLine(carId, payload);
      return { dispatched: true, topic, payload, error: null };
    } catch (err) {
      return {
        dispatched: false,
        topic,
        payload,
        error: err?.message || String(err),
      };
    }
  }

  applyInboundLine(carId, line) {
    if (!CAR_ID_RE.test(carId)) {
      return;
    }
    const mirror = this.mirrorFor(carId);
    const t = trim(line);
    if (!t || t.startsWith("#")) {
      return;
    }

    let changed = false;

    if (t.startsWith("{")) {
      try {
        const j = JSON.parse(t);
        if (j.command === "KILL_SWITCH") {
          let feature = "";
          if (j.payload && typeof j.payload === "object") {
            feature = j.payload.feature_name || j.payload.logical_name || "";
          }
          mirror.applyKillLogical(toLogicalFeatureName(feature));
          changed = true;
        }
      } catch {
        /* ignore */
      }
      if (changed) {
        this.publishMirrorState(carId);
      }
      return;
    }

    const colon = t.indexOf(":");
    const type = colon === -1 ? t : t.slice(0, colon);
    const rest = colon === -1 ? "" : t.slice(colon + 1);

    if (type === "KILL" || type === "KILL_SWITCH") {
      mirror.applyKillLogical(toLogicalFeatureName(rest));
      changed = true;
    } else if (type === "FLAG_ON") {
      const logical = toLogicalFeatureName(logicalBaseFromFlagRest(rest));
      if (!mirror.killedLogical.has(logical)) {
        mirror.applyFlagOnRest(rest);
        changed = true;
      }
    } else if (type === "FLAG_OFF") {
      mirror.applyFlagOffRest(rest);
      changed = true;
    }

    if (changed) {
      this.publishMirrorState(carId);
    }
  }

  get(carId) {
    const mirror = this.mirrors.get(carId);
    if (!mirror) {
      return null;
    }
    return mirror.toStatePayload();
  }

  has(carId) {
    return this.mirrors.has(carId);
  }
}

/** When WSL car_N process is not running, all domain flags show OFF (ignore stale MQTT retain). */
function forceOfflineFeatureFlags(states, probeMap, warnings) {
  let count = 0;
  for (const st of states) {
    const carId = st?.car_id;
    if (!carId || !CAR_ID_RE.test(carId)) {
      continue;
    }
    const procUp = probeMap && probeMap[carId] === true;
    if (procUp) {
      continue;
    }
    if (!st.features || typeof st.features !== "object") {
      st.features = {};
    }
    for (const deployKey of Object.values(LOGICAL_TO_DEPLOY)) {
      const prev = st.features[deployKey] || {};
      st.features[deployKey] = {
        ...prev,
        flag: "OFF",
        flag_source: "vehicle_offline",
      };
    }
    if (st.status === "online") {
      st.status = "offline";
    }
    count += 1;
  }
  if (count > 0) {
    warnings.push(`fleet flags: ${count} vehicle(s) offline — System Status flags forced OFF`);
  }
}

function mergeMqttFlagsIntoStates(states, cache, probeMap, warnings) {
  let merged = 0;
  let missing = 0;
  for (const st of states) {
    const carId = st?.car_id;
    if (!carId || !CAR_ID_RE.test(carId)) {
      continue;
    }
    const procUp = probeMap && probeMap[carId] === true;
    if (!procUp) {
      continue;
    }
    const mqttPayload = cache.get(carId);
    if (!mqttPayload?.features) {
      missing += 1;
      continue;
    }
    st.mqtt_flags = mqttPayload;
    if (!st.features || typeof st.features !== "object") {
      st.features = {};
    }
    for (const [logical, deployKey] of Object.entries(LOGICAL_TO_DEPLOY)) {
      const mf = mqttPayload.features[logical];
      if (!mf || typeof mf.main !== "boolean") {
        continue;
      }
      const prev = st.features[deployKey] || {};
      st.features[deployKey] = {
        ...prev,
        flag: mf.main ? "ON" : "OFF",
        flag_source: "mqtt",
      };
    }
    merged += 1;
  }

  if (merged > 0) {
    warnings.push(`fleet flags: merged MQTT mirror for ${merged} vehicle(s)`);
  }
  if (missing > 0 && !cache.connected) {
    warnings.push(`fleet flags: no MQTT mirror for ${missing} vehicle(s) (using deploy/state only)`);
  }
  if (!cache.connected && cache.lastError) {
    warnings.push(`fleet flags: MQTT offline (${cache.lastError}) — deploy/state flags only`);
  } else if (!cache.connected) {
    warnings.push("fleet flags: MQTT not connected — deploy/state flags only");
  }
}

const mqttFlagCache = new MqttFlagCache();

module.exports = {
  mqttFlagCache,
  LOGICAL_TO_DEPLOY,
  forceOfflineFeatureFlags,
  mergeMqttFlagsIntoStates,
  CAR_ID_RE,
};
