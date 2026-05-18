const express = require("express");
const cors = require("cors");
const fs = require("fs");
const path = require("path");
const { execFile } = require("child_process");
const http = require("http");
const https = require("https");
const { mqttFlagCache, forceOfflineFeatureFlags, mergeMqttFlagsIntoStates } = require("./mqttFlagCache");

const app = express();
const PORT = Number(process.env.PORT || 8080);
const REPO_ROOT = process.env.SDV_ROOT || path.resolve(__dirname, "..");
const DOMAINS = ["ADAS", "NAV", "ENERGY"];
/** `sdv-ota-manager` (sibling of `sdv-feature-flag-platform`) — Docker Primary/Shadow demo */
const OTA_MANAGER_ROOT = path.resolve(REPO_ROOT, "..", "sdv-ota-manager");
/** Platform-style deployment artifacts moved under sdv-ota-manager/platform/deploy. */
const PLATFORM_DEPLOY_ROOT = path.join(OTA_MANAGER_ROOT, "platform");
const DEPLOY_ROOT = path.join(PLATFORM_DEPLOY_ROOT, "deploy");
const OTA_VEHICLE_SCRIPT = path.join(OTA_MANAGER_ROOT, "scripts", "ota_vehicle.sh");
const OTA_VEHICLE_RUNTIME_DIR = path.join(OTA_MANAGER_ROOT, "deploy", "vehicle_runtime");
const PENDING_DOCKER_ROLLOUT_PATH = path.join(OTA_VEHICLE_RUNTIME_DIR, "pending_docker_rollout.json");
const FLEET_DOCKER_VERSIONS_PATH = path.join(OTA_VEHICLE_RUNTIME_DIR, "fleet_feature_versions.json");
const APPLY_FLEET_DOCKER_PY = path.join(OTA_MANAGER_ROOT, "scripts", "apply_fleet_feature_versions.py");
const RUNTIME_DOCKER_CAR_RE = /^car_(10|[1-9])$/;

app.use(cors());
app.use(express.json({ limit: "256kb" }));
// If a client sends invalid JSON, don't crash the server; return 400.
app.use((err, _req, res, next) => {
  if (err && err.type === "entity.parse.failed") {
    res.status(400).json({ ok: false, error: "invalid JSON body" });
    return;
  }
  next(err);
});

// ---- Unleash Admin API proxy (Token auth) ----
//
// The dashboard (5173) calls these endpoints via Vite proxy -> status-server (8080).
// status-server then calls Unleash Admin API (default http://127.0.0.1:4242) with an API token.
//
// Env:
//   - UNLEASH_ADMIN_URL: base URL of Unleash (default http://127.0.0.1:4242)
//   - UNLEASH_API_TOKEN: admin/project token (sent as Authorization header)
//
const UNLEASH_ADMIN_URL = String(process.env.UNLEASH_ADMIN_URL || "http://127.0.0.1:4242").trim();
const UNLEASH_API_TOKEN = String(process.env.UNLEASH_API_TOKEN || "").trim();

function requireUnleashToken(res) {
  if (!UNLEASH_API_TOKEN) {
    res.status(503).json({
      ok: false,
      error: "UNLEASH_API_TOKEN is not set (status-server cannot call Unleash Admin API).",
    });
    return false;
  }
  return true;
}

function unleashUrlFor(pathname) {
  const base = new URL(UNLEASH_ADMIN_URL);
  const p = String(pathname || "");
  // Preserve base path if any, and avoid double slashes.
  base.pathname = `${base.pathname.replace(/\/$/, "")}/${p.replace(/^\//, "")}`;
  return base;
}

function readJsonSafe(text) {
  try {
    return JSON.parse(String(text || ""));
  } catch {
    return null;
  }
}

function unleashRequest(method, pathname, body) {
  return new Promise((resolve, reject) => {
    const url = unleashUrlFor(pathname);
    const lib = url.protocol === "https:" ? https : http;
    const payload = body == null ? "" : JSON.stringify(body);
    const req = lib.request(
      {
        protocol: url.protocol,
        hostname: url.hostname,
        port: url.port || (url.protocol === "https:" ? 443 : 80),
        path: `${url.pathname}${url.search || ""}`,
        method: String(method || "GET").toUpperCase(),
        headers: {
          Authorization: UNLEASH_API_TOKEN,
          "content-type": "application/json",
          "user-agent": "sdv-status-server/unleash-proxy",
        },
        timeout: 10_000,
      },
      (resp) => {
        let buf = "";
        resp.setEncoding("utf8");
        resp.on("data", (chunk) => {
          buf += chunk;
        });
        resp.on("end", () => {
          const json = readJsonSafe(buf);
          resolve({
            status: resp.statusCode || 0,
            ok: resp.statusCode >= 200 && resp.statusCode < 300,
            json,
            text: buf,
          });
        });
      },
    );
    req.on("error", reject);
    if (payload) {
      req.write(payload);
    }
    req.end();
  });
}

function normalizeProjectId(v) {
  const s = String(v || "").trim();
  return s || "default";
}

function normalizeEnvironment(v) {
  const s = String(v || "").trim();
  // Unleash default env is often "development"; keep a sensible default.
  return s || "development";
}

function normalizeFeatureName(v) {
  const s = String(v || "").trim();
  return s;
}

function asStringArray(v) {
  if (Array.isArray(v)) return v.map((x) => String(x).trim()).filter(Boolean);
  if (typeof v === "string") {
    return v
      .split(/[,;]/)
      .map((s) => s.trim())
      .filter(Boolean);
  }
  return [];
}

function clampInt(n, lo, hi, fallback) {
  const x = Number(n);
  if (!Number.isFinite(x)) return fallback;
  return Math.max(lo, Math.min(hi, Math.round(x)));
}

function makeConstraint(contextName, values) {
  return {
    contextName: String(contextName || "").trim(),
    operator: "IN",
    values: (values || []).map(String),
  };
}

function makeFlexibleRolloutStrategy({ title, contextName, values, rollout, stickiness, groupId }) {
  return {
    name: "flexibleRollout",
    title: title || null,
    constraints: [makeConstraint(contextName, values)],
    parameters: {
      rollout: String(clampInt(rollout, 0, 100, 100)),
      stickiness: String(stickiness || "userId"),
      groupId: String(groupId || ""),
    },
  };
}

async function getStrategies(projectId, featureName, environment) {
  return unleashRequest(
    "GET",
    `/api/admin/projects/${encodeURIComponent(projectId)}/features/${encodeURIComponent(featureName)}/environments/${encodeURIComponent(environment)}/strategies`,
  );
}

async function addStrategy(projectId, featureName, environment, body) {
  return unleashRequest(
    "POST",
    `/api/admin/projects/${encodeURIComponent(projectId)}/features/${encodeURIComponent(featureName)}/environments/${encodeURIComponent(environment)}/strategies`,
    body,
  );
}

async function updateStrategy(projectId, featureName, environment, strategyId, body) {
  return unleashRequest(
    "PUT",
    `/api/admin/projects/${encodeURIComponent(projectId)}/features/${encodeURIComponent(featureName)}/environments/${encodeURIComponent(environment)}/strategies/${encodeURIComponent(strategyId)}`,
    body,
  );
}

async function overwriteVariants(projectId, featureName, environment, variants) {
  return unleashRequest(
    "PUT",
    `/api/admin/projects/${encodeURIComponent(projectId)}/features/${encodeURIComponent(featureName)}/environments/${encodeURIComponent(environment)}/variants`,
    variants,
  );
}

function pickExistingTargetStrategy(strategies, contextName) {
  const list = Array.isArray(strategies) ? strategies : [];
  const want = String(contextName || "").trim();
  for (const s of list) {
    if (!s || typeof s !== "object") continue;
    if (String(s.name || "") !== "flexibleRollout") continue;
    const cs = Array.isArray(s.constraints) ? s.constraints : [];
    if (cs.some((c) => c && String(c.contextName || "") === want && String(c.operator || "") === "IN")) {
      return s;
    }
  }
  return null;
}

function pickExistingStrategyByGroupId(strategies, groupId) {
  const list = Array.isArray(strategies) ? strategies : [];
  const gid = String(groupId || "").trim();
  if (!gid) return null;
  for (const s of list) {
    if (!s || typeof s !== "object") continue;
    const p = s.parameters || {};
    if (p && typeof p === "object" && String(p.groupId || "") === gid) {
      return s;
    }
  }
  return null;
}

function shuffleInPlace(arr) {
  for (let i = arr.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}

function allocateCountsByPercent(total, percents) {
  // Largest remainder method to ensure integer counts sum to total.
  const raw = percents.map((p) => (total * p) / 100);
  const base = raw.map((x) => Math.floor(x));
  let used = base.reduce((a, b) => a + b, 0);
  const remainders = raw.map((x, i) => ({ i, r: x - Math.floor(x) }));
  remainders.sort((a, b) => b.r - a.r);
  let left = total - used;
  let k = 0;
  while (left > 0 && k < remainders.length * 2) {
    const idx = remainders[k % remainders.length].i;
    base[idx] += 1;
    left -= 1;
    k += 1;
  }
  return base;
}

// 1) Targeting rollout by region/model via strategy constraints (flexibleRollout).
app.post("/api/unleash/strategy/target", async (req, res) => {
  if (!requireUnleashToken(res)) return;

  const body = req.body || {};
  const projectId = normalizeProjectId(body.projectId);
  const environment = normalizeEnvironment(body.environment);
  const featureName = normalizeFeatureName(body.featureName);
  const contextName = String(body.contextName || body.targetKey || "").trim(); // "region" | "model"
  const values = asStringArray(body.values || body.value || body.targets);
  const rollout = clampInt(body.rollout ?? body.percentage ?? 100, 0, 100, 100);
  const stickiness = String(body.stickiness || "userId").trim() || "userId";

  if (!featureName) {
    res.status(400).json({ ok: false, error: "featureName is required" });
    return;
  }
  if (!contextName || (contextName !== "region" && contextName !== "model" && contextName !== "group")) {
    res.status(400).json({ ok: false, error: 'contextName must be "region", "model", or "group"' });
    return;
  }
  if (!values.length) {
    res.status(400).json({ ok: false, error: "values must be a non-empty list" });
    return;
  }

  try {
    const st = await getStrategies(projectId, featureName, environment);
    if (!st.ok) {
      const upstreamMsg =
        (st.json && (st.json.message || st.json.error || st.json.name)) ||
        (typeof st.text === "string" && st.text.trim()) ||
        "";
      res.status(st.status || 500).json({
        ok: false,
        error: `Unleash get strategies failed (${st.status})${upstreamMsg ? `: ${upstreamMsg}` : ""}`,
        upstream: st.json || st.text,
      });
      return;
    }

    const strategies = Array.isArray(st.json) ? st.json : [];
    const existing = pickExistingTargetStrategy(strategies, contextName);
    const title = `SDV target: ${contextName} IN [${values.join(", ")}] rollout=${rollout}%`;
    const groupId = `sdv-${featureName}-${contextName}`;
    const strategyBody = makeFlexibleRolloutStrategy({
      title,
      contextName,
      values,
      rollout,
      stickiness,
      groupId,
    });

    let out;
    if (existing && existing.id) {
      out = await updateStrategy(projectId, featureName, environment, existing.id, strategyBody);
    } else {
      out = await addStrategy(projectId, featureName, environment, strategyBody);
    }

    res.status(out.ok ? 200 : out.status || 500).json({
      ok: out.ok,
      projectId,
      environment,
      featureName,
      mode: existing ? "updated" : "created",
      upstream_status: out.status,
      upstream: out.json || out.text,
      error: out.ok
        ? undefined
        : `Unleash strategy write failed (${out.status})` +
          (((out.json && (out.json.message || out.json.error)) || (out.text && String(out.text).trim()))
            ? `: ${(out.json && (out.json.message || out.json.error)) || String(out.text).trim()}`
            : ""),
    });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message || String(err) });
  }
});

// 2) A/B ratio settings: overwrite feature variants for an environment.
app.post("/api/unleash/strategy/variants", async (req, res) => {
  if (!requireUnleashToken(res)) return;

  const body = req.body || {};
  const projectId = normalizeProjectId(body.projectId);
  const environment = normalizeEnvironment(body.environment);
  const featureName = normalizeFeatureName(body.featureName);
  const stickiness = String(body.stickiness || "userId").trim() || "userId";

  if (!featureName) {
    res.status(400).json({ ok: false, error: "featureName is required" });
    return;
  }

  // Accept either { variants: [{name, weight}], ... } with weights in 0..1000
  // or { ratioA: 50, ratioB: 50 } (percentage).
  let variants = [];
  if (Array.isArray(body.variants) && body.variants.length) {
    variants = body.variants
      .map((v) => ({
        name: String(v?.name || "").trim(),
        weight: clampInt(v?.weight, 0, 1000, 0),
        stickiness,
      }))
      .filter((v) => v.name && v.weight >= 0);
  } else {
    const a = clampInt(body.ratioA ?? body.a ?? 50, 0, 100, 50);
    const b = clampInt(body.ratioB ?? body.b ?? 50, 0, 100, 50);
    const sum = Math.max(1, a + b);
    const wA = clampInt((a / sum) * 1000, 0, 1000, 500);
    const wB = clampInt(1000 - wA, 0, 1000, 500);
    variants = [
      { name: String(body.nameA || "A"), weight: wA, stickiness },
      { name: String(body.nameB || "B"), weight: wB, stickiness },
    ];
  }

  if (!variants.length) {
    res.status(400).json({ ok: false, error: "variants must be provided (or ratioA/ratioB)" });
    return;
  }

  try {
    const out = await overwriteVariants(projectId, featureName, environment, variants);
    res.status(out.ok ? 200 : out.status || 500).json({
      ok: out.ok,
      projectId,
      environment,
      featureName,
      upstream_status: out.status,
      upstream: out.json || out.text,
      error: out.ok
        ? undefined
        : `Unleash variants overwrite failed (${out.status})` +
          (((out.json && (out.json.message || out.json.error)) || (out.text && String(out.text).trim()))
            ? `: ${(out.json && (out.json.message || out.json.error)) || String(out.text).trim()}`
            : ""),
    });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message || String(err) });
  }
});

// 3) Promote: force one variant to 100% (weight=1000).
app.post("/api/unleash/promote", async (req, res) => {
  if (!requireUnleashToken(res)) return;

  const body = req.body || {};
  const projectId = normalizeProjectId(body.projectId);
  const environment = normalizeEnvironment(body.environment);
  const featureName = normalizeFeatureName(body.featureName);
  const winner = String(body.winner || body.variant || "").trim();
  const stickiness = String(body.stickiness || "userId").trim() || "userId";

  if (!featureName) {
    res.status(400).json({ ok: false, error: "featureName is required" });
    return;
  }
  if (!winner) {
    res.status(400).json({ ok: false, error: "winner (variant name) is required" });
    return;
  }

  try {
    const out = await overwriteVariants(projectId, featureName, environment, [
      { name: winner, weight: 1000, stickiness },
    ]);
    res.status(out.ok ? 200 : out.status || 500).json({
      ok: out.ok,
      projectId,
      environment,
      featureName,
      winner,
      upstream_status: out.status,
      upstream: out.json || out.text,
      error: out.ok
        ? undefined
        : `Unleash promote failed (${out.status})` +
          (((out.json && (out.json.message || out.json.error)) || (out.text && String(out.text).trim()))
            ? `: ${(out.json && (out.json.message || out.json.error)) || String(out.text).trim()}`
            : ""),
    });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message || String(err) });
  }
});

// Option 2: target-by-context and deploy a concrete version to matching cars.
// - Writes a flexibleRollout strategy with a single strategy-level variant (name=version token)
// - Executes Docker OTA promote (ota_vehicle.sh promote) per matched car
app.post("/api/unleash/deploy/target-version", async (req, res) => {
  if (!requireUnleashToken(res)) return;

  const body = req.body || {};
  const projectId = normalizeProjectId(body.projectId);
  const environment = normalizeEnvironment(body.environment);
  const featureName = normalizeFeatureName(body.featureName);
  const contextName = String(body.contextName || body.targetKey || "").trim();
  const values = asStringArray(body.values || body.value || body.targets);
  const versionRaw = String(body.version || "").trim();
  const version = versionRaw.replace(/^v(?=\d)/i, "");

  if (!featureName) return res.status(400).json({ ok: false, error: "featureName is required" });
  if (!contextName || (contextName !== "region" && contextName !== "model" && contextName !== "group")) {
    return res.status(400).json({ ok: false, error: 'contextName must be "region", "model", or "group"' });
  }
  if (!values.length) return res.status(400).json({ ok: false, error: "values must be a non-empty list" });
  if (!version) return res.status(400).json({ ok: false, error: "version is required (e.g. 2.0)" });

  const domainMap = {
    NavigationFeature: "Navigation",
    AdasFeature: "ADAS",
    EvEnergyFeature: "EvEnergy",
  };
  const domain = domainMap[featureName];
  if (!domain) {
    return res.status(400).json({
      ok: false,
      error: 'featureName must be one of "NavigationFeature", "AdasFeature", "EvEnergyFeature"',
    });
  }

  const vehiclesPath = path.join(REPO_ROOT, "config", "fleet", "vehicles.json");
  let vehicles;
  try {
    const raw = readJson(vehiclesPath);
    vehicles = Array.isArray(raw?.vehicles) ? raw.vehicles : [];
  } catch (err) {
    return res.status(500).json({ ok: false, error: `failed to read config/fleet/vehicles.json: ${err.message}` });
  }

  const matched = vehicles
    .filter((v) => v && typeof v === "object")
    .filter((v) => {
      const key = String(contextName);
      const val = String(v[key] || "").trim();
      return val && values.includes(val);
    })
    .map((v) => String(v.car_id || "").trim())
    .filter((id) => /^car_(10|[1-9])$/.test(id));

  if (!matched.length) {
    return res.status(400).json({ ok: false, error: `no cars matched ${contextName} IN [${values.join(", ")}]` });
  }

  // 1) Upsert Unleash strategy with a single strategy-level variant = desired version token (observability).
  let unleashWrite = null;
  try {
    const st = await getStrategies(projectId, featureName, environment);
    if (!st.ok) {
      unleashWrite = { ok: false, status: st.status, upstream: st.json || st.text };
    } else {
      const strategies = Array.isArray(st.json) ? st.json : [];
      const gid = `sdv-${featureName}-${contextName}-ver-${values.join("_")}`;
      const existing = pickExistingStrategyByGroupId(strategies, gid) || pickExistingTargetStrategy(strategies, contextName);
      const title = `SDV version target: ${contextName} IN [${values.join(", ")}] -> v${version}`;
      const strategyBody = makeFlexibleRolloutStrategy({
        title,
        contextName,
        values,
        rollout: 100,
        stickiness: "userId",
        groupId: gid,
      });
      // Strategy-level variants take precedence over environment variants.
      strategyBody.variants = [
        { name: version, weight: 1000, weightType: "fix", stickiness: "userId" },
      ];

      let out;
      if (existing && existing.id) {
        out = await updateStrategy(projectId, featureName, environment, existing.id, strategyBody);
        unleashWrite = { ok: out.ok, mode: "updated", status: out.status, upstream: out.json || out.text };
      } else {
        out = await addStrategy(projectId, featureName, environment, strategyBody);
        unleashWrite = { ok: out.ok, mode: "created", status: out.status, upstream: out.json || out.text };
      }
    }
  } catch (err) {
    unleashWrite = { ok: false, error: err.message || String(err) };
  }

  // 2) Execute Docker OTA promote for matched cars (actual runtime change).
  const results = [];
  for (const car_id of matched) {
    try {
      const r = await execVehicleRuntimeWsl("promote", { car_id, domain, version, sync_shadow: false });
      results.push({
        car_id,
        ok: Number(r.code) === 0,
        exit_code: r.code,
        stderr: (r.stderr || "").trim(),
        stdout: (r.stdout || "").trim(),
      });
    } catch (err) {
      results.push({ car_id, ok: false, exit_code: -1, stderr: err.message || String(err), stdout: "" });
    }
  }

  const success = results.filter((x) => x.ok).map((x) => x.car_id);
  const failed = results.filter((x) => !x.ok).map((x) => x.car_id);

  res.json({
    ok: success.length > 0 && failed.length === 0,
    featureName,
    domain,
    contextName,
    values,
    version,
    matched,
    success_targets: success,
    failed_targets: failed,
    unleash_strategy_write: unleashWrite,
    results,
  });
});

// Region -> Version deploy in one request (KR/US/EU).
app.post("/api/unleash/deploy/region-versions", async (req, res) => {
  if (!requireUnleashToken(res)) return;

  const body = req.body || {};
  const projectId = normalizeProjectId(body.projectId);
  const environment = normalizeEnvironment(body.environment);
  const featureName = normalizeFeatureName(body.featureName);
  const versionsByRegion = body.versionsByRegion && typeof body.versionsByRegion === "object" ? body.versionsByRegion : {};

  if (!featureName) return res.status(400).json({ ok: false, error: "featureName is required" });

  const domainMap = {
    NavigationFeature: "Navigation",
    AdasFeature: "ADAS",
    EvEnergyFeature: "EvEnergy",
  };
  const domain = domainMap[featureName];
  if (!domain) {
    return res.status(400).json({
      ok: false,
      error: 'featureName must be one of "NavigationFeature", "AdasFeature", "EvEnergyFeature"',
    });
  }

  const wanted = {};
  for (const r of ["KR", "US", "EU"]) {
    const raw = String(versionsByRegion[r] ?? "").trim();
    const v = raw.replace(/^v(?=\d)/i, "");
    if (v) wanted[r] = v;
  }
  const regions = Object.keys(wanted);
  if (!regions.length) {
    return res.status(400).json({ ok: false, error: "No region versions provided (KR/US/EU are all empty)." });
  }

  let vehicles;
  try {
    const raw = readJson(path.join(REPO_ROOT, "config", "fleet", "vehicles.json"));
    vehicles = Array.isArray(raw?.vehicles) ? raw.vehicles : [];
  } catch (err) {
    return res.status(500).json({ ok: false, error: `failed to read config/fleet/vehicles.json: ${err.message}` });
  }

  const byRegion = { KR: [], US: [], EU: [] };
  for (const v of vehicles) {
    if (!v || typeof v !== "object") continue;
    const id = String(v.car_id || "").trim();
    const reg = String(v.region || "").trim();
    if (!/^car_(10|[1-9])$/.test(id)) continue;
    if (byRegion[reg]) byRegion[reg].push(id);
  }

  // 1) Upsert Unleash strategy per region (observability: strategy-level variant name = version).
  const unleashWrites = {};
  try {
    const st = await getStrategies(projectId, featureName, environment);
    const strategies = st.ok && Array.isArray(st.json) ? st.json : [];
    for (const r of regions) {
      const version = wanted[r];
      const values = [r];
      const gid = `sdv-${featureName}-region-${r}`;
      const existing = pickExistingStrategyByGroupId(strategies, gid);
      const title = `SDV region version: ${r} -> v${version}`;
      const strategyBody = makeFlexibleRolloutStrategy({
        title,
        contextName: "region",
        values,
        rollout: 100,
        stickiness: "userId",
        groupId: gid,
      });
      strategyBody.variants = [
        { name: version, weight: 1000, weightType: "fix", stickiness: "userId" },
      ];

      let out;
      if (existing && existing.id) {
        out = await updateStrategy(projectId, featureName, environment, existing.id, strategyBody);
        unleashWrites[r] = { ok: out.ok, mode: "updated", status: out.status, upstream: out.json || out.text };
      } else {
        out = await addStrategy(projectId, featureName, environment, strategyBody);
        unleashWrites[r] = { ok: out.ok, mode: "created", status: out.status, upstream: out.json || out.text };
      }
    }
  } catch (err) {
    for (const r of regions) {
      unleashWrites[r] = { ok: false, error: err.message || String(err) };
    }
  }

  // 2) Execute Docker OTA promote per matched car.
  const results = [];
  for (const r of regions) {
    const version = wanted[r];
    const cars = byRegion[r] || [];
    for (const car_id of cars) {
      try {
        const rr = await execVehicleRuntimeWsl("promote", { car_id, domain, version, sync_shadow: false });
        results.push({
          region: r,
          version,
          car_id,
          ok: Number(rr.code) === 0,
          exit_code: rr.code,
          stderr: (rr.stderr || "").trim(),
          stdout: (rr.stdout || "").trim(),
        });
      } catch (e) {
        results.push({
          region: r,
          version,
          car_id,
          ok: false,
          exit_code: -1,
          stderr: e.message || String(e),
          stdout: "",
        });
      }
    }
  }

  const success = results.filter((x) => x.ok).map((x) => x.car_id);
  const failed = results.filter((x) => !x.ok).map((x) => x.car_id);

  res.json({
    ok: failed.length === 0,
    featureName,
    domain,
    versionsByRegion: wanted,
    matched_by_region: Object.fromEntries(regions.map((r) => [r, byRegion[r] || []])),
    unleash_strategy_write: unleashWrites,
    success_targets: success,
    failed_targets: failed,
    results,
  });
});

// Global version deploy (all cars).
app.post("/api/unleash/deploy/global-version", async (req, res) => {
  if (!requireUnleashToken(res)) return;

  const body = req.body || {};
  const projectId = normalizeProjectId(body.projectId);
  const environment = normalizeEnvironment(body.environment);
  const featureName = normalizeFeatureName(body.featureName);
  const versionRaw = String(body.version || "").trim();
  const version = versionRaw.replace(/^v(?=\d)/i, "");
  if (!featureName) return res.status(400).json({ ok: false, error: "featureName is required" });
  if (!version) return res.status(400).json({ ok: false, error: "version is required" });

  const domainMap = {
    NavigationFeature: "Navigation",
    AdasFeature: "ADAS",
    EvEnergyFeature: "EvEnergy",
  };
  const domain = domainMap[featureName];
  if (!domain) {
    return res.status(400).json({
      ok: false,
      error: 'featureName must be one of "NavigationFeature", "AdasFeature", "EvEnergyFeature"',
    });
  }

  let vehicles;
  try {
    const raw = readJson(path.join(REPO_ROOT, "config", "fleet", "vehicles.json"));
    vehicles = Array.isArray(raw?.vehicles) ? raw.vehicles : [];
  } catch (err) {
    return res.status(500).json({ ok: false, error: `failed to read config/fleet/vehicles.json: ${err.message}` });
  }
  const cars = vehicles
    .map((v) => String(v?.car_id || "").trim())
    .filter((id) => /^car_(10|[1-9])$/.test(id));

  // Optional: write a broad Unleash strategy "region IN [KR,US,EU] -> vX" for observability.
  let unleashWrite = null;
  try {
    const st = await getStrategies(projectId, featureName, environment);
    if (st.ok) {
      const strategies = Array.isArray(st.json) ? st.json : [];
      const gid = `sdv-${featureName}-global`;
      const existing = pickExistingStrategyByGroupId(strategies, gid);
      const strategyBody = makeFlexibleRolloutStrategy({
        title: `SDV global version -> v${version}`,
        contextName: "region",
        values: ["KR", "US", "EU"],
        rollout: 100,
        stickiness: "userId",
        groupId: gid,
      });
      strategyBody.variants = [{ name: version, weight: 1000, weightType: "fix", stickiness: "userId" }];
      const out = existing && existing.id
        ? await updateStrategy(projectId, featureName, environment, existing.id, strategyBody)
        : await addStrategy(projectId, featureName, environment, strategyBody);
      unleashWrite = { ok: out.ok, status: out.status, upstream: out.json || out.text };
    }
  } catch (err) {
    unleashWrite = { ok: false, error: err.message || String(err) };
  }

  const results = [];
  for (const car_id of cars) {
    try {
      const rr = await execVehicleRuntimeWsl("promote", { car_id, domain, version, sync_shadow: false });
      results.push({
        car_id,
        ok: Number(rr.code) === 0,
        exit_code: rr.code,
        stderr: (rr.stderr || "").trim(),
        stdout: (rr.stdout || "").trim(),
      });
    } catch (e) {
      results.push({ car_id, ok: false, exit_code: -1, stderr: e.message || String(e), stdout: "" });
    }
  }

  const success = results.filter((x) => x.ok).map((x) => x.car_id);
  const failed = results.filter((x) => !x.ok).map((x) => x.car_id);

  res.json({
    ok: failed.length === 0,
    featureName,
    domain,
    version,
    matched: cars,
    unleash_strategy_write: unleashWrite,
    success_targets: success,
    failed_targets: failed,
    results,
  });
});

// Model -> Version deploy in one request (IONIQ5/IONIQ6/KONA_EV/...).
app.post("/api/unleash/deploy/model-versions", async (req, res) => {
  if (!requireUnleashToken(res)) return;

  const body = req.body || {};
  const projectId = normalizeProjectId(body.projectId);
  const environment = normalizeEnvironment(body.environment);
  const featureName = normalizeFeatureName(body.featureName);
  const versionsByModel = body.versionsByModel && typeof body.versionsByModel === "object" ? body.versionsByModel : {};

  if (!featureName) return res.status(400).json({ ok: false, error: "featureName is required" });

  const domainMap = {
    NavigationFeature: "Navigation",
    AdasFeature: "ADAS",
    EvEnergyFeature: "EvEnergy",
  };
  const domain = domainMap[featureName];
  if (!domain) {
    return res.status(400).json({
      ok: false,
      error: 'featureName must be one of "NavigationFeature", "AdasFeature", "EvEnergyFeature"',
    });
  }

  let vehicles;
  try {
    const raw = readJson(path.join(REPO_ROOT, "config", "fleet", "vehicles.json"));
    vehicles = Array.isArray(raw?.vehicles) ? raw.vehicles : [];
  } catch (err) {
    return res.status(500).json({ ok: false, error: `failed to read config/fleet/vehicles.json: ${err.message}` });
  }

  const knownModels = [...new Set(vehicles.map((v) => String(v?.model || "").trim()).filter(Boolean))].sort();
  const wanted = {};
  for (const m of knownModels) {
    const raw = String(versionsByModel[m] ?? "").trim();
    const v = raw.replace(/^v(?=\d)/i, "");
    if (v) wanted[m] = v;
  }
  const models = Object.keys(wanted);
  if (!models.length) {
    return res.status(400).json({ ok: false, error: "No model versions provided (all empty)." });
  }

  const carsByModel = Object.fromEntries(models.map((m) => [m, []]));
  for (const v of vehicles) {
    if (!v || typeof v !== "object") continue;
    const id = String(v.car_id || "").trim();
    const model = String(v.model || "").trim();
    if (!/^car_(10|[1-9])$/.test(id)) continue;
    if (carsByModel[model]) carsByModel[model].push(id);
  }

  // 1) Upsert Unleash strategy per model (observability: strategy-level variant name = version).
  const unleashWrites = {};
  try {
    const st = await getStrategies(projectId, featureName, environment);
    const strategies = st.ok && Array.isArray(st.json) ? st.json : [];
    for (const m of models) {
      const version = wanted[m];
      const values = [m];
      const gid = `sdv-${featureName}-model-${m}`;
      const existing = pickExistingStrategyByGroupId(strategies, gid);
      const title = `SDV model version: ${m} -> v${version}`;
      const strategyBody = makeFlexibleRolloutStrategy({
        title,
        contextName: "model",
        values,
        rollout: 100,
        stickiness: "userId",
        groupId: gid,
      });
      strategyBody.variants = [{ name: version, weight: 1000, weightType: "fix", stickiness: "userId" }];

      let out;
      if (existing && existing.id) {
        out = await updateStrategy(projectId, featureName, environment, existing.id, strategyBody);
        unleashWrites[m] = { ok: out.ok, mode: "updated", status: out.status, upstream: out.json || out.text };
      } else {
        out = await addStrategy(projectId, featureName, environment, strategyBody);
        unleashWrites[m] = { ok: out.ok, mode: "created", status: out.status, upstream: out.json || out.text };
      }
    }
  } catch (err) {
    for (const m of models) {
      unleashWrites[m] = { ok: false, error: err.message || String(err) };
    }
  }

  // 2) Execute Docker OTA promote per matched car.
  const results = [];
  for (const m of models) {
    const version = wanted[m];
    const cars = carsByModel[m] || [];
    for (const car_id of cars) {
      try {
        const rr = await execVehicleRuntimeWsl("promote", { car_id, domain, version, sync_shadow: false });
        results.push({
          model: m,
          version,
          car_id,
          ok: Number(rr.code) === 0,
          exit_code: rr.code,
          stderr: (rr.stderr || "").trim(),
          stdout: (rr.stdout || "").trim(),
        });
      } catch (e) {
        results.push({
          model: m,
          version,
          car_id,
          ok: false,
          exit_code: -1,
          stderr: e.message || String(e),
          stdout: "",
        });
      }
    }
  }

  const success = results.filter((x) => x.ok).map((x) => x.car_id);
  const failed = results.filter((x) => !x.ok).map((x) => x.car_id);

  res.json({
    ok: failed.length === 0,
    featureName,
    domain,
    versionsByModel: wanted,
    matched_by_model: carsByModel,
    unleash_strategy_write: unleashWrites,
    success_targets: success,
    failed_targets: failed,
    results,
  });
});

// Ratio-based rollout: random per-car assignment by percent + promote.
app.post("/api/unleash/deploy/ratio-versions", async (req, res) => {
  if (!requireUnleashToken(res)) return;

  const body = req.body || {};
  const projectId = normalizeProjectId(body.projectId);
  const environment = normalizeEnvironment(body.environment);
  const featureName = normalizeFeatureName(body.featureName);
  const stickiness = String(body.stickiness || "userId").trim() || "userId";
  const splits = Array.isArray(body.splits) ? body.splits : [];

  if (!featureName) return res.status(400).json({ ok: false, error: "featureName is required" });
  if (!splits.length) return res.status(400).json({ ok: false, error: "splits must be a non-empty list" });

  const domainMap = {
    NavigationFeature: "Navigation",
    AdasFeature: "ADAS",
    EvEnergyFeature: "EvEnergy",
  };
  const domain = domainMap[featureName];
  if (!domain) {
    return res.status(400).json({
      ok: false,
      error: 'featureName must be one of "NavigationFeature", "AdasFeature", "EvEnergyFeature"',
    });
  }

  const cleaned = splits
    .map((s) => ({
      percent: clampInt(s?.percent ?? s?.ratio ?? 0, 0, 100, 0),
      version: String(s?.version || "").trim().replace(/^v(?=\d)/i, ""),
    }))
    .filter((s) => s.percent > 0 && s.version);

  if (!cleaned.length) {
    return res.status(400).json({ ok: false, error: "splits must include percent>0 and non-empty version" });
  }

  const sum = cleaned.reduce((a, b) => a + b.percent, 0);
  if (sum !== 100) {
    return res.status(400).json({ ok: false, error: `split percent sum must equal 100 (got ${sum})` });
  }

  let vehicles;
  try {
    const raw = readJson(path.join(REPO_ROOT, "config", "fleet", "vehicles.json"));
    vehicles = Array.isArray(raw?.vehicles) ? raw.vehicles : [];
  } catch (err) {
    return res.status(500).json({ ok: false, error: `failed to read config/fleet/vehicles.json: ${err.message}` });
  }

  const cars = vehicles
    .map((v) => String(v?.car_id || "").trim())
    .filter((id) => /^car_(10|[1-9])$/.test(id));
  if (!cars.length) {
    return res.status(400).json({ ok: false, error: "no cars found in vehicles.json" });
  }

  // Observability: update Unleash environment variants weights to match ratios (weights 0..1000).
  let variantsWrite = null;
  try {
    const variants = cleaned.map((s) => ({
      name: s.version,
      weight: clampInt((s.percent / 100) * 1000, 0, 1000, 0),
      stickiness,
    }));
    // Ensure sum weights == 1000 by adjusting last.
    const wSum = variants.reduce((a, b) => a + (b.weight || 0), 0);
    if (variants.length) variants[variants.length - 1].weight = clampInt((variants[variants.length - 1].weight || 0) + (1000 - wSum), 0, 1000, 0);
    const out = await overwriteVariants(projectId, featureName, environment, variants);
    variantsWrite = { ok: out.ok, status: out.status, upstream: out.json || out.text, variants };
  } catch (err) {
    variantsWrite = { ok: false, error: err.message || String(err) };
  }

  // Random assignment to cars based on percent.
  const shuffled = shuffleInPlace([...cars]);
  const counts = allocateCountsByPercent(shuffled.length, cleaned.map((s) => s.percent));
  const assignment = [];
  let cursor = 0;
  for (let i = 0; i < cleaned.length; i++) {
    const take = counts[i] || 0;
    const version = cleaned[i].version;
    for (const car_id of shuffled.slice(cursor, cursor + take)) {
      assignment.push({ car_id, version });
    }
    cursor += take;
  }

  // Execute promote per assigned car.
  const results = [];
  for (const a of assignment) {
    try {
      const rr = await execVehicleRuntimeWsl("promote", { car_id: a.car_id, domain, version: a.version, sync_shadow: false });
      results.push({
        car_id: a.car_id,
        version: a.version,
        ok: Number(rr.code) === 0,
        exit_code: rr.code,
        stderr: (rr.stderr || "").trim(),
        stdout: (rr.stdout || "").trim(),
      });
    } catch (e) {
      results.push({ car_id: a.car_id, version: a.version, ok: false, exit_code: -1, stderr: e.message || String(e), stdout: "" });
    }
  }

  const success = results.filter((x) => x.ok).map((x) => x.car_id);
  const failed = results.filter((x) => !x.ok).map((x) => x.car_id);

  // Summary per version.
  const byVersion = {};
  for (const r of results) {
    const v = r.version;
    if (!byVersion[v]) byVersion[v] = { total: 0, success: 0, failed: 0 };
    byVersion[v].total += 1;
    if (r.ok) byVersion[v].success += 1;
    else byVersion[v].failed += 1;
  }

  res.json({
    ok: failed.length === 0,
    featureName,
    domain,
    splits: cleaned,
    matched: cars,
    assignment,
    summary_by_version: byVersion,
    unleash_variants_write: variantsWrite,
    success_targets: success,
    failed_targets: failed,
    results,
  });
});

function toWslPath(winPath) {
  const normalized = path.resolve(winPath);
  const parsed = path.parse(normalized);
  const drive = (parsed.root || "").slice(0, 2);
  if (!/^[A-Za-z]:$/.test(drive)) {
    throw new Error(`Unsupported path for WSL conversion: ${normalized}`);
  }
  const driveLetter = drive[0].toLowerCase();
  const rest = normalized.slice(2).replace(/\\/g, "/");
  return `/mnt/${driveLetter}${rest}`;
}

function execWslBash(args, options = {}) {
  const { wslDistro, ...execOpts } = options;
  const envDistro = process.env.SDV_WSL_DISTRO && String(process.env.SDV_WSL_DISTRO).trim();
  const distro =
    wslDistro && String(wslDistro).trim().length > 0
      ? String(wslDistro).trim()
      : envDistro && envDistro.length > 0
        ? envDistro
        : "Ubuntu";
  return new Promise((resolve, reject) => {
    execFile(
      "wsl.exe",
      ["-d", distro, "--", "bash", "-lc", args],
      { windowsHide: true, maxBuffer: 5 * 1024 * 1024, ...execOpts },
      (err, stdout, stderr) => {
        // Non-zero exit codes are expected for "partial failures" (fleet deploy),
        // so we always resolve with the captured output.
        if (err && typeof err.code === "number") {
          resolve({ code: err.code, stdout, stderr });
          return;
        }
        if (err) {
          reject(err);
          return;
        }
        resolve({ code: 0, stdout, stderr });
      }
    );
  });
}

function detectDeploymentMgrPath() {
  const envPreferred = process.env.SDV_DEPLOYMENT_MGR_EXE && String(process.env.SDV_DEPLOYMENT_MGR_EXE).trim();
  const envFallback = process.env.DEPLOYMENT_MGR_EXE && String(process.env.DEPLOYMENT_MGR_EXE).trim();

  // Candidate search list (in priority order). Keep platform build fallbacks for backwards compatibility.
  const candidates = [
    envPreferred,
    envFallback,
    // Independent module build (preferred when platform does not build DeploymentManager).
    path.resolve(REPO_ROOT, "..", "sdv-ota-manager", "platform", "DeploymentManager", "build", "DeploymentMgr"),
    // Legacy platform builds.
    path.join(REPO_ROOT, "build_unleash", "DeploymentMgr"),
    path.join(REPO_ROOT, "build", "DeploymentMgr"),
  ]
    .filter(Boolean)
    .map((p) => path.resolve(String(p)));

  const tried = [];
  for (const p of candidates) {
    tried.push(p);
    if (fs.existsSync(p)) {
      return { winPath: p, wslPath: toWslPath(p), tried };
    }
  }

  const msg =
    "DeploymentMgr executable not found.\n" +
    "Tried:\n" +
    tried.map((p) => `  - ${p}`).join("\n") +
    "\n\n" +
    "Fix options:\n" +
    "  - Set SDV_DEPLOYMENT_MGR_EXE to the full Windows path of DeploymentMgr\n" +
    "    Example: setx SDV_DEPLOYMENT_MGR_EXE \"C:\\path\\to\\DeploymentMgr\"\n" +
    "  - Or build the independent module:\n" +
    "    sdv-ota-manager/platform/DeploymentManager/build/DeploymentMgr\n" +
    "  - Or build the platform targets (legacy):\n" +
    "    sdv-feature-flag-platform/build_unleash/DeploymentMgr";
  const err = new Error(msg);
  err.tried = tried;
  throw err;
}

function readJson(filePath) {
  const raw = fs.readFileSync(filePath, "utf8");
  return JSON.parse(raw);
}

function readText(filePath) {
  try {
    return fs.readFileSync(filePath, "utf8");
  } catch (_) {
    return "";
  }
}

// Warning-aware helper for primary deployment state files.
function readTextWithWarning(filePath, warningLabel, warnings) {
  try {
    return fs.readFileSync(filePath, "utf8");
  } catch (_) {
    warnings.push(`missing ${warningLabel}`);
    return "";
  }
}

function readLines(filePath) {
  return readText(filePath)
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
}

function tail(array, count) {
  return array.length <= count ? array : array.slice(array.length - count);
}

function parseKeyValueFile(filePath, warnings) {
  const result = {};
  for (const line of readTextWithWarning(
    filePath,
    path.relative(REPO_ROOT, filePath).replace(/\\/g, "/"),
    warnings
  )
    .split(/\r?\n/)
    .map((entry) => entry.trim())
    .filter(Boolean)) {
    const eq = line.indexOf("=");
    if (eq === -1) {
      continue;
    }
    const key = line.slice(0, eq).trim();
    const value = line.slice(eq + 1).trim();
    result[key] = value;
  }
  return result;
}

function readInventory(warnings) {
  return DOMAINS.map((domain) => {
    const statePath = path.join(DEPLOY_ROOT, "metadata", `inventory_${domain}.state`);
    const data = parseKeyValueFile(statePath, warnings);

    return {
      domain,
      candidate_version: data.candidate_version || "A",
      active_version: data.active_version || "A",
      stable_version: data.stable_version || "A",
      last_deployment_status: data.last_deployment_status || "STABLE",
      rollback_available: (data.rollback_available || "no") === "yes",
      last_deployed_at: data.last_deployed_at || null,
      state_file: path.relative(REPO_ROOT, statePath).replace(/\\/g, "/"),
    };
  });
}

function readSelection(warnings) {
  const selectionPath = path.join(DEPLOY_ROOT, "active_selection.conf");
  const selection = {
    ADAS: "A",
    NAV: "A",
    ENERGY: "A",
  };

  for (const line of readTextWithWarning(
    selectionPath,
    path.relative(REPO_ROOT, selectionPath).replace(/\\/g, "/"),
    warnings
  )
    .split(/\r?\n/)
    .map((entry) => entry.trim())
    .filter(Boolean)) {
    if (line.startsWith("#")) {
      continue;
    }
    const [key, value] = line.split(/\s+/);
    if (selection[key] && (value === "A" || value === "B")) {
      selection[key] = value;
    }
  }

  return {
    values: selection,
    selection_file: path.relative(REPO_ROOT, selectionPath).replace(/\\/g, "/"),
  };
}

function readActiveBinaries(warnings) {
  const activeDir = path.join(DEPLOY_ROOT, "active");
  try {
    return fs
      .readdirSync(activeDir, { withFileTypes: true })
      .filter((entry) => entry.isFile())
      .map((entry) => entry.name)
      .sort();
  } catch (_) {
    warnings.push(`missing ${path.relative(REPO_ROOT, activeDir).replace(/\\/g, "/")}`);
    return [];
  }
}

function parseHistoryLine(line) {
  const [action, feature, version, exec, status, mode, deployedAt, source, destination] =
    line.split("|");
  const event = {
    action: action || "",
    event_type: action || "unknown",
    feature: feature || "",
    version: version || "",
    exec: exec || "",
    status: status || "",
    mode: mode || "",
    deployed_at: deployedAt || "",
    source: source || "",
    destination: destination || "",
    raw: line,
  };

  if (event.action === "deploy") {
    event.summary = "deployment";
  } else if (event.action === "select") {
    event.summary = "release_select";
  } else if (event.action === "rollback") {
    event.summary = "release_rollback";
  } else if (event.action === "health_check") {
    event.summary = "release_health_check";
  } else {
    event.summary = "unknown";
  }

  if (event.action === "select" || event.action === "rollback") {
    event.selection_target = feature || "";
    event.selection_version = version || "";
  }

  if (event.action === "health_check") {
    event.health_target = exec || "";
    event.health_result = status || "";
  }

  return event;
}

function readRecentHistory(limit = 5, warnings = []) {
  const historyPath = path.join(DEPLOY_ROOT, "history", "deployment.log");
  return tail(
    readTextWithWarning(
      historyPath,
      path.relative(REPO_ROOT, historyPath).replace(/\\/g, "/"),
      warnings
    )
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter(Boolean),
    limit
  ).map(parseHistoryLine);
}

function parseManifestLine(line) {
  const [feature, version, exec, deployedPath, deployedAt, status, mode] = line.split("|");
  return {
    feature: feature || "",
    version: version || "",
    exec: exec || "",
    deployed_path: deployedPath || "",
    deployed_at: deployedAt || "",
    status: status || "",
    mode: mode || "",
    raw: line,
  };
}

function readRecentManifest(limit = 5, warnings = []) {
  const manifestPath = path.join(DEPLOY_ROOT, "manifest.txt");
  return tail(
    readTextWithWarning(
      manifestPath,
      path.relative(REPO_ROOT, manifestPath).replace(/\\/g, "/"),
      warnings
    )
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter(Boolean),
    limit
  ).map(parseManifestLine);
}

app.get("/api/status", (_req, res) => {
  const warnings = [];
  const inventory = readInventory(warnings);
  const inventoryMap = Object.fromEntries(inventory.map((entry) => [entry.domain, entry]));
  const selection = readSelection(warnings);
  const activeBinaries = readActiveBinaries(warnings);
  const recentHistory = readRecentHistory(5, warnings);
  const recentManifest = readRecentManifest(5, warnings);

  res.json({
    service: "sdv-status-server",
    repo_root: REPO_ROOT.replace(/\\/g, "/"),
    deploy_root: DEPLOY_ROOT.replace(/\\/g, "/"),
    warnings: [...new Set(warnings)],
    inventory,
    inventory_map: inventoryMap,
    selection,
    active_binaries: activeBinaries,
    recent_history: recentHistory,
    recent_manifest: recentManifest,
  });
});

// ---- Fleet APIs (read/write) ----

app.get("/api/fleet/vehicles", (_req, res) => {
  const warnings = [];
  const filePath = path.join(REPO_ROOT, "config", "fleet", "vehicles.json");
  try {
    if (!fs.existsSync(filePath)) {
      warnings.push("missing config/fleet/vehicles.json");
      return res.json({ ok: true, vehicles: [], raw: null, warnings });
    }
    const data = readJson(filePath);
    res.json({ ok: true, vehicles: data.vehicles || [], raw: data, warnings });
  } catch (err) {
    warnings.push(`vehicles.json: ${err.message}`);
    res.json({ ok: false, vehicles: [], raw: null, warnings });
  }
});

app.get("/api/fleet/version-map", (_req, res) => {
  const warnings = [];
  const filePath = path.join(DEPLOY_ROOT, "version_map.json");
  try {
    if (!fs.existsSync(filePath)) {
      warnings.push("missing deploy/version_map.json");
      return res.json({ ok: true, raw: null, warnings });
    }
    const data = readJson(filePath);
    res.json({ ok: true, raw: data, warnings });
  } catch (err) {
    warnings.push(`version_map.json: ${err.message}`);
    res.json({ ok: false, raw: null, warnings });
  }
});

const PACKAGE_REGISTRY_PATH = path.join(DEPLOY_ROOT, "package_registry.json");
const PKG_FEATURES = new Set(["ADAS", "Navigation", "Energy"]);
const PKG_ROLES = new Set(["candidate", "stable", "experimental"]);
const PKG_ARTIFACT_TYPES = new Set(["docker_image", "executable"]);

function dockerTagVersionFromVersionToken(versionToken) {
  const v = String(versionToken || "").trim();
  if (!v) return "";
  if ((v[0] === "v" || v[0] === "V") && v[1] !== undefined && /[0-9]/.test(v[1])) {
    return `v${v.slice(1)}`;
  }
  return `v${v}`;
}

function normalizeFeatureKey(raw) {
  // Make feature comparisons robust across user input styles:
  // - case-insensitive
  // - accept EvEnergy / EVENERGY / evenergy / energy variants
  // - strip separators like "_" and "-"
  return String(raw || "")
    .trim()
    .toLowerCase()
    .replace(/[\s_-]+/g, "");
}

function defaultDockerRepoForFeature(feature) {
  const f = normalizeFeatureKey(feature);
  if (f === "adas") return "sdv/adas-feature";
  if (f === "navigation" || f === "nav") return "sdv/navigation-feature";
  if (f === "energy" || f === "evenergy" || f === "ev") return "sdv/evenergy-feature";
  return `sdv/${f || "unknown"}-feature`;
}

function normalizeArtifactType(raw, fallback) {
  const t = String(raw || "").trim();
  if (!t) return fallback;
  if (PKG_ARTIFACT_TYPES.has(t)) return t;
  return fallback;
}

function defaultPackageRegistry() {
  return { schema_version: 1, packages: [] };
}

function readPackageRegistryDisk(warnings) {
  try {
    if (!fs.existsSync(PACKAGE_REGISTRY_PATH)) {
      const def = defaultPackageRegistry();
      fs.mkdirSync(path.dirname(PACKAGE_REGISTRY_PATH), { recursive: true });
      fs.writeFileSync(PACKAGE_REGISTRY_PATH, `${JSON.stringify(def, null, 2)}\n`, "utf8");
      return def;
    }
    return readJson(PACKAGE_REGISTRY_PATH);
  } catch (err) {
    warnings.push(`package_registry.json: ${err.message}`);
    return defaultPackageRegistry();
  }
}

function writePackageRegistryDisk(data) {
  fs.mkdirSync(path.dirname(PACKAGE_REGISTRY_PATH), { recursive: true });
  fs.writeFileSync(PACKAGE_REGISTRY_PATH, `${JSON.stringify(data, null, 2)}\n`, "utf8");
}

function toStringArray(val) {
  if (Array.isArray(val)) return val.map((x) => String(x).trim()).filter(Boolean);
  if (typeof val === "string") {
    return val
      .split(/[,;]/)
      .map((s) => s.trim())
      .filter(Boolean);
  }
  return [];
}

function safePathSegment(seg) {
  const s = String(seg || "").trim();
  if (!s || !/^[a-zA-Z0-9._-]+$/.test(s)) return null;
  if (s.includes("..")) return null;
  return s;
}

function validateManifestShape(m) {
  const required = [
    "package_id",
    "feature",
    "version",
    "role",
    "binary",
    "description",
    "compatible_models",
    "compatible_regions",
    "requires_safety_check",
    "install_policy",
    "rollback",
  ];
  for (const k of required) {
    if (m[k] === undefined || m[k] === null) {
      return `missing manifest field: ${k}`;
    }
  }
  if (!PKG_FEATURES.has(String(m.feature))) {
    return "feature must be ADAS, Navigation, or Energy";
  }
  if (!String(m.version).trim()) {
    return "version is required";
  }
  if (!String(m.package_id).trim()) {
    return "package_id is required";
  }
  if (!String(m.binary).trim()) {
    return "binary is required";
  }
  if (!PKG_ROLES.has(String(m.role))) {
    return "role must be candidate, stable, or experimental";
  }
  if (!Array.isArray(m.compatible_models) || !m.compatible_models.length) {
    return "compatible_models must be a non-empty array";
  }
  if (!Array.isArray(m.compatible_regions) || !m.compatible_regions.length) {
    return "compatible_regions must be a non-empty array";
  }
  if (typeof m.requires_safety_check !== "boolean") {
    return "requires_safety_check must be boolean";
  }
  const ip = m.install_policy;
  if (!ip || typeof ip !== "object") {
    return "install_policy must be an object";
  }
  if (typeof ip.allow_driving_download !== "boolean") {
    return "install_policy.allow_driving_download must be boolean";
  }
  const ir = ip.install_requires;
  if (!ir || typeof ir !== "object") {
    return "install_policy.install_requires must be an object";
  }
  const rb = m.rollback;
  if (!rb || typeof rb !== "object") {
    return "rollback must be an object";
  }
  if (rb.fallback_slot !== "A" && rb.fallback_slot !== "B") {
    return "rollback.fallback_slot must be A or B";
  }
  if (typeof rb.fallback_required !== "boolean") {
    return "rollback.fallback_required must be boolean";
  }
  return "";
}

app.get("/api/fleet/package-registry", (_req, res) => {
  const warnings = [];
  const data = readPackageRegistryDisk(warnings);
  const packages = Array.isArray(data.packages) ? data.packages : [];
  res.json({
    ok: true,
    packages,
    raw: data,
    warnings,
    file: "deploy/package_registry.json",
  });
});

app.post("/api/fleet/package-register", (req, res) => {
  const body = req.body || {};
  const feature = String(body.feature || "").trim();
  const versionRaw = String(body.version || "").trim();
  const role = String(body.role || "").trim();
  const binary = String(body.binary || "").trim();
  const description = String(body.description || "").trim();
  const package_id_in = String(body.package_id || "").trim();

  if (!PKG_FEATURES.has(feature)) {
    res.status(400).json({ ok: false, error: "feature must be ADAS, Navigation, or Energy" });
    return;
  }
  const version = safePathSegment(versionRaw);
  if (!version) {
    res.status(400).json({ ok: false, error: "invalid version (use alphanumerics, dot, underscore, hyphen only)" });
    return;
  }
  if (!PKG_ROLES.has(role)) {
    res.status(400).json({ ok: false, error: "role must be candidate, stable, or experimental" });
    return;
  }
  if (!binary) {
    res.status(400).json({ ok: false, error: "binary is required" });
    return;
  }
  if (!description) {
    res.status(400).json({ ok: false, error: "description is required" });
    return;
  }

  const compatible_models = toStringArray(body.compatible_models);
  const compatible_regions = toStringArray(body.compatible_regions);
  if (!compatible_models.length) {
    res.status(400).json({ ok: false, error: "compatible_models must be a non-empty list" });
    return;
  }
  if (!compatible_regions.length) {
    res.status(400).json({ ok: false, error: "compatible_regions must be a non-empty list" });
    return;
  }

  const requires_safety_check = body.requires_safety_check === true;
  const allow_driving_download = body.allow_driving_download === true;
  const min_battery = Math.max(0, Math.min(100, Number(body.min_battery) || 30));
  const fallback_slot = body.fallback_slot === "B" ? "B" : "A";

  const now = new Date().toISOString();
  const package_id =
    package_id_in ||
    `${feature}_${version}_${now.slice(0, 10).replace(/-/g, "")}_${now.slice(11, 19).replace(/:/g, "")}`;

  const versionDir = versionRaw[0] === "v" || versionRaw[0] === "V" ? version : `v${version}`;
  const package_path = `deploy/packages/${feature}/${versionDir}`;
  const manifest_path = `${package_path}/manifest.json`;

  // ---- Docker-oriented metadata (backward-compatible, optional inputs) ----
  const artifact_type = normalizeArtifactType(body.artifact_type, "docker_image");
  const docker_image_in = String(body.docker_image || "").trim();
  const docker_image =
    docker_image_in ||
    `${defaultDockerRepoForFeature(feature)}:${dockerTagVersionFromVersionToken(version) || "v0.0"}`;
  const image_digest = String(body.image_digest || "").trim();
  const image_digest_source = String(body.image_digest_source || "").trim();
  const checksum_file = String(body.checksum_file || "").trim() || "checksum.sha256";
  const checksum = String(body.checksum || "").trim();
  const supported_targets = Array.isArray(body.supported_targets) && body.supported_targets.length
    ? body.supported_targets.map((x) => String(x).trim()).filter(Boolean)
    : ["docker"];
  const binary_name = String(body.binary_name || "").trim() || binary;
  const executable_name = String(body.executable_name || "").trim() || binary;

  const manifest = {
    // NOTE: manifest.schema.json uses "domain" to mean the high-level feature domain (ADAS/Navigation/Energy).
    // For this UI/server, "feature" already carries that domain label; we write both for compatibility.
    domain: feature,
    package_id,
    feature,
    version,
    role,
    binary,
    artifact_type,
    docker_image,
    image_digest,
    image_digest_source,
    checksum_file,
    checksum,
    executable_name,
    binary_name,
    supported_targets,
    package_path,
    manifest_path,
    description,
    created_at: now,
    registered_at: now,
    compatible_models,
    compatible_regions,
    requires_safety_check,
    install_policy: {
      allow_driving_download,
      install_requires: {
        vehicle_mode: "PARKED",
        ignition: "OFF",
        update_mode: "READY",
        gear: "P",
        speed: 0,
        parking_brake: "ON",
        min_battery,
        driver_approved: true,
        network: "CONNECTED",
        target_system_running: false,
      },
    },
    rollback: {
      fallback_slot,
      fallback_required: true,
    },
  };

  const shapeErr = validateManifestShape(manifest);
  if (shapeErr) {
    res.status(400).json({ ok: false, error: `manifest validation failed: ${shapeErr}` });
    return;
  }

  const warnings = [];
  const reg = readPackageRegistryDisk(warnings);
  const packages = Array.isArray(reg.packages) ? [...reg.packages] : [];

  if (packages.some((p) => p.package_id === package_id)) {
    res.status(400).json({ ok: false, error: `duplicate package_id: ${package_id}` });
    return;
  }
  if (packages.some((p) => p.feature === feature && p.version === version)) {
    res.status(400).json({
      ok: false,
      error: `duplicate feature+version: ${feature} ${version} (overwrite not supported in this step)`,
    });
    return;
  }

  const dirAbs = path.join(DEPLOY_ROOT, "packages", feature, version);
  try {
    fs.mkdirSync(dirAbs, { recursive: true });
    const manifestAbs = path.join(dirAbs, "manifest.json");
    fs.writeFileSync(manifestAbs, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
  } catch (err) {
    res.status(500).json({ ok: false, error: `failed to write package files: ${err.message}` });
    return;
  }

  packages.push(manifest);
  reg.packages = packages;
  try {
    writePackageRegistryDisk(reg);
  } catch (err) {
    res.status(500).json({ ok: false, error: `failed to update package_registry.json: ${err.message}` });
    return;
  }

  res.json({
    ok: true,
    package: {
      package_id,
      feature,
      version,
      package_path,
      manifest_path,
    },
    registry_path: "deploy/package_registry.json",
    warnings,
  });
});

app.post("/api/fleet/version-map/assign", (req, res) => {
  const body = req.body || {};
  const package_id = String(body.package_id || "").trim();
  const slotRaw = String(body.slot || "").trim().toUpperCase();
  const featureIn = String(body.feature || "").trim();

  if (!package_id) {
    res.status(400).json({ ok: false, error: "package_id is required" });
    return;
  }
  if (slotRaw !== "A" && slotRaw !== "B") {
    res.status(400).json({ ok: false, error: "slot must be A or B" });
    return;
  }

  const warnings = [];
  const reg = readPackageRegistryDisk(warnings);
  const packages = Array.isArray(reg.packages) ? reg.packages : [];
  const pkg = packages.find((p) => p && String(p.package_id) === package_id);
  if (!pkg) {
    res.status(400).json({ ok: false, error: `package_id not found: ${package_id}` });
    return;
  }
  const feature = String(pkg.feature || "").trim();
  if (!PKG_FEATURES.has(feature)) {
    res.status(400).json({ ok: false, error: "invalid package.feature" });
    return;
  }
  if (featureIn && featureIn !== feature) {
    res.status(400).json({
      ok: false,
      error: `feature mismatch: request feature ${featureIn} does not match package feature ${feature}`,
    });
    return;
  }
  const version = String(pkg.version || "").trim();
  const binary = String(pkg.binary || "").trim();
  const role = String(pkg.role || "").trim();
  if (!version || !binary || !role) {
    res.status(400).json({ ok: false, error: "package entry missing version, binary, or role" });
    return;
  }
  if (!PKG_ROLES.has(role)) {
    res.status(400).json({ ok: false, error: "package has invalid role" });
    return;
  }

  const vmRel = "deploy/version_map.json";
  const vmPath = path.join(DEPLOY_ROOT, "version_map.json");
  const vmBakPath = `${vmPath}.bak`;
  if (!fs.existsSync(vmPath)) {
    res.status(400).json({ ok: false, error: "deploy/version_map.json not found" });
    return;
  }
  let vm;
  try {
    vm = readJson(vmPath);
  } catch (err) {
    res.status(500).json({ ok: false, error: `failed to read version_map.json: ${err.message}` });
    return;
  }
  if (!vm.features || typeof vm.features !== "object") {
    res.status(500).json({ ok: false, error: "version_map.json invalid: missing features" });
    return;
  }
  if (!vm.features[feature] || typeof vm.features[feature] !== "object") {
    res.status(400).json({ ok: false, error: `version_map missing feature entry: ${feature}` });
    return;
  }
  if (!vm.features[feature][slotRaw] || typeof vm.features[feature][slotRaw] !== "object") {
    res.status(400).json({ ok: false, error: `version_map missing ${feature}.${slotRaw} slot` });
    return;
  }

  const previous = { ...vm.features[feature][slotRaw] };
  const vDir = version[0] === "v" || version[0] === "V" ? version : `v${version}`;
  const package_path = String(pkg.package_path || "").trim() || `deploy/packages/${feature}/${vDir}`;
  const assigned = {
    version,
    binary,
    role,
    package_id: String(pkg.package_id),
    package_path,
  };

  vm.features[feature][slotRaw] = assigned;

  try {
    fs.copyFileSync(vmPath, vmBakPath);
  } catch (err) {
    res.status(500).json({ ok: false, error: `failed to backup version_map.json: ${err.message}` });
    return;
  }
  try {
    fs.writeFileSync(vmPath, `${JSON.stringify(vm, null, 2)}\n`, "utf8");
  } catch (err) {
    try {
      fs.copyFileSync(vmBakPath, vmPath);
    } catch (_) {
      /* best effort restore */
    }
    res.status(500).json({ ok: false, error: `failed to write version_map.json: ${err.message}` });
    return;
  }

  res.json({
    ok: true,
    feature,
    slot: slotRaw,
    previous,
    assigned,
    version_map_path: vmRel,
    backup_path: "deploy/version_map.json.bak",
    warnings,
  });
});

/**
 * Best-effort: detect native `car_N` parent processes in WSL (same layout as run_sdv.ps1).
 * Returns map car_id -> boolean for car_1..car_10 when WSL succeeds (even if all false).
 * Empty map only when WSL cannot run — then the dashboard keeps file-based `status` (often all "online").
 */
async function probeFleetCarProcessesWsl(warnings) {
  const empty = {};
  const distro = process.env.SDV_WSL_DISTRO;
  const inner =
    "{ pgrep -af /build_unleash/car_ 2>/dev/null || true; pgrep -af /build/car_ 2>/dev/null || true; }";
  try {
    const { code, stdout, stderr } = await execWslBash(inner, { wslDistro: distro });
    if (code !== 0) {
      warnings.push(
        `fleet runtime probe: wsl/bash exit ${code}: ${String(stderr || "")
          .trim()
          .slice(0, 240)}`,
      );
      return empty;
    }
    const running = new Set();
    const re = /\/(?:build_unleash|build)\/car_(10|[1-9])\b/;
    for (const line of String(stdout || "").split(/\r?\n/)) {
      const m = line.match(re);
      if (m) {
        running.add(`car_${m[1]}`);
      }
    }
    const out = {};
    for (let i = 1; i <= 10; i += 1) {
      const id = `car_${i}`;
      out[id] = running.has(id);
    }
    return out;
  } catch (e) {
    warnings.push(`fleet runtime probe failed: ${e.message || String(e)}`);
    return empty;
  }
}

function mergeRuntimeProbeIntoStates(states, probeMap, warnings) {
  const keys = Object.keys(probeMap || {});
  if (!keys.length) {
    return;
  }
  const iso = new Date().toISOString();
  for (const st of states) {
    const id = st && st.car_id;
    if (!id || typeof probeMap[id] !== "boolean") continue;
    st.runtime = {
      car_process_running: probeMap[id],
      probed_at: iso,
      source: "wsl_pgrep",
    };
  }
  const expected = states.filter((s) => s && s.car_id && RUNTIME_DOCKER_CAR_RE.test(s.car_id)).length;
  const probed = states.filter((s) => s && typeof s.runtime?.car_process_running === "boolean").length;
  if (expected > 0 && probed > 0 && probed < expected) {
    warnings.push(`fleet runtime probe: partial (${probed}/${expected} cars got a pgrep result)`);
  }
}

function lastMatchingLine(filePath, predicate) {
  if (!fs.existsSync(filePath)) {
    return "";
  }
  const lines = readText(filePath)
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
  for (let i = lines.length - 1; i >= 0; i -= 1) {
    if (predicate(lines[i])) {
      return lines[i];
    }
  }
  return "";
}

function parseLocationLine(line) {
  const m = String(line || "").match(/(?:차량 위치 전송|현재 위치 수신|차량 위치 수신):\s*(.+?)\s*\(([-\d.]+),\s*([-\d.]+)\)/);
  if (!m) return null;
  return {
    label: m[1],
    lat: Number(m[2]),
    lon: Number(m[3]),
    raw: line,
  };
}

function parseRoadLine(line) {
  const m = String(line || "").match(/(?:도로 정보 전송|도로 정보 수신):\s*(.+?)(?:\s*\((.*)\))?$/);
  if (!m) return null;
  const detail = m[2] || "";
  const speedLimitMatch = detail.match(/제한속도\s*(\d+)km\/h/);
  return {
    summary: m[1],
    detail,
    speed_limit: speedLimitMatch ? Number(speedLimitMatch[1]) : 80,
    raw: line,
  };
}

/** Last `RANGE: ... 배터리 N% | Mkm` line from EvEnergyFeature.log (live demo drain/charge). */
function parseEnergyBatteryFromLine(line) {
  const m = String(line || "").match(/배터리\s*(-?\d+(?:\.\d+)?)\s*%/);
  if (!m) return null;
  const pct = Number(m[1]);
  return Number.isFinite(pct) ? pct : null;
}

function parseEnergyRangeKmFromLine(line) {
  const m = String(line || "").match(/\|\s*(-?\d+)\s*km/);
  if (!m) return null;
  const km = Number(m[1]);
  return Number.isFinite(km) ? km : null;
}

function latestEnergyBatteryFromLog(carId) {
  const logPath = path.join(REPO_ROOT, "logs", carId, "EvEnergyFeature.log");
  const line = lastMatchingLine(logPath, (l) => /배터리\s*-?\d/.test(l));
  if (!line) {
    return null;
  }
  const pct = parseEnergyBatteryFromLine(line);
  if (pct == null) {
    return null;
  }
  return {
    battery_pct: pct,
    range_km: parseEnergyRangeKmFromLine(line),
    raw: line,
  };
}

/**
 * Overlay deploy/state OTA battery with live EvEnergy RANGE lines when the WSL car_N process
 * is actually running. Stale log files must not keep the UI in "live" mode after shutdown.
 */
function mergeLiveEnergyTelemetryIntoStates(states, probeMap, warnings) {
  const iso = new Date().toISOString();
  for (const st of states) {
    const id = st && st.car_id;
    if (!id || !/^car_(10|[1-9])$/.test(id)) {
      continue;
    }
    const procUp = probeMap && typeof probeMap[id] === "boolean" ? probeMap[id] : false;
    if (!procUp) {
      if (st.features?.Energy) {
        delete st.features.Energy.live_from_log;
      }
      if (st.ota) {
        delete st.ota.battery_source;
        delete st.ota.battery_observed_at;
      }
      continue;
    }
    const live = latestEnergyBatteryFromLog(id);
    if (!live) {
      continue;
    }
    if (!st.ota || typeof st.ota !== "object") {
      st.ota = {};
    }
    const clamped = Math.max(0, Math.min(100, Math.round(live.battery_pct)));
    st.ota.battery = clamped;
    st.ota.battery_source = "EvEnergyFeature.log";
    st.ota.battery_observed_at = iso;
    if (live.range_km != null) {
      st.ota.range_km = live.range_km;
    }
    if (!st.features || typeof st.features !== "object") {
      st.features = {};
    }
    if (!st.features.Energy || typeof st.features.Energy !== "object") {
      st.features.Energy = {};
    }
    st.features.Energy.live_from_log = true;
  }
  const merged = states.filter((s) => s?.ota?.battery_source === "EvEnergyFeature.log").length;
  if (merged > 0) {
    warnings.push(`fleet energy telemetry: merged live battery for ${merged} vehicle(s) from EvEnergyFeature.log`);
  }
}

function latestCockpitLogTelemetry(carId) {
  const logDir = path.join(REPO_ROOT, "logs", carId);
  const navLog = path.join(logDir, "NavigationFeature.log");
  const adasLog = path.join(logDir, "AdasFeature.log");
  const energyLog = path.join(logDir, "EvEnergyFeature.log");

  const navLocation = parseLocationLine(lastMatchingLine(navLog, (line) => line.includes("차량 위치 전송")));
  const adasLocation = parseLocationLine(lastMatchingLine(adasLog, (line) => line.includes("현재 위치 수신")));
  const energyLocation = parseLocationLine(lastMatchingLine(energyLog, (line) => line.includes("차량 위치 수신")));
  const navRoad = parseRoadLine(lastMatchingLine(navLog, (line) => line.includes("도로 정보 전송")));
  const adasRoad = parseRoadLine(lastMatchingLine(adasLog, (line) => line.includes("도로 정보 수신")));
  const energyRoad = parseRoadLine(lastMatchingLine(energyLog, (line) => line.includes("도로 정보 수신")));

  const energyModeLine = lastMatchingLine(
    energyLog,
    (line) => line.includes("평지 주행") || line.includes("오르막 감지") || line.includes("내리막 감지"),
  );

  return {
    location: navLocation || adasLocation || energyLocation,
    road: navRoad || adasRoad || energyRoad,
    adas: {
      location: adasLocation,
      road: adasRoad,
      latest: lastMatchingLine(adasLog, (line) => line.includes("자율주행 중") || line.includes("LKA:") || line.includes("SCC:")),
    },
    navigation: {
      location: navLocation,
      road: navRoad,
    },
    energy: {
      location: energyLocation,
      road: energyRoad,
      mode: energyModeLine,
    },
  };
}

app.get("/api/fleet/cockpit", async (_req, res) => {
  const carId = String(_req.query.car_id || "car_1").trim();
  if (!/^car_(10|[1-9])$/.test(carId)) {
    res.status(400).json({ ok: false, error: "car_id must be car_1 .. car_10" });
    return;
  }

  const warnings = [];
  let state = null;
  try {
    state = readJson(path.join(DEPLOY_ROOT, "state", `${carId}.json`));
  } catch (err) {
    warnings.push(`fleet state unavailable: ${err.message}`);
  }

  const probeMap = await probeFleetCarProcessesWsl(warnings);
  const procUp = probeMap && typeof probeMap[carId] === "boolean" ? probeMap[carId] : false;
  const vehicleLive = procUp === true;

  if (state) {
    const enriched = [{ ...state, car_id: carId }];
    mergeRuntimeProbeIntoStates(enriched, probeMap, warnings);
    forceOfflineFeatureFlags(enriched, probeMap, warnings);
    mergeMqttFlagsIntoStates(enriched, mqttFlagCache, probeMap, warnings);
    state = enriched[0];
  }

  const ota = state?.ota || {};
  const features = state?.features || {};
  const energy = features.Energy || {};
  const nav = features.Navigation || {};
  const adas = features.ADAS || {};
  const logs = vehicleLive ? latestCockpitLogTelemetry(carId) : {};
  const liveEnergy = vehicleLive ? latestEnergyBatteryFromLog(carId) : null;
  let battery = 0;
  let batterySource = "offline";
  if (vehicleLive) {
    battery = Math.max(0, Math.min(100, Math.round(Number(ota.battery) || 82)));
    batterySource = "deploy/state";
    if (liveEnergy) {
      battery = Math.max(0, Math.min(100, Math.round(liveEnergy.battery_pct)));
      batterySource = "EvEnergyFeature.log";
    }
  }
  let speed = Number(ota.speed);
  if (vehicleLive && (!Number.isFinite(speed) || speed < 1)) {
    speed = Math.max(52, Math.min(92, (logs.road?.speed_limit || 100) - 28));
  } else if (!Number.isFinite(speed) || speed < 1) {
    speed = 0;
  }

  res.json({
    ok: true,
    car_id: carId,
    vehicle_process_running: procUp,
    data_mode: vehicleLive ? "live" : "offline",
    speed: Math.round(speed),
    battery,
    battery_source: batterySource,
    range_km: vehicleLive
      ? liveEnergy?.range_km != null
        ? liveEnergy.range_km
        : Math.round(battery * 3.8)
      : 0,
    gear: vehicleLive ? ota.gear || "D" : "P",
    status: vehicleLive ? state?.status || "online" : "offline",
    location: vehicleLive ? logs.location || null : null,
    road: vehicleLive ? logs.road || null : null,
    route_distance: vehicleLive
      ? logs.road?.summary?.match(/(\d+\s*km|\d+\s*m)/)?.[1] || "120 m"
      : "—",
    route_direction: vehicleLive ? logs.location?.label || "판교 / 분당 방면" : "차량 정지",
    features: {
      adas: {
        active_version: adas.active_version || "v3.0",
        running: String(adas.flag || "ON").toUpperCase() === "ON",
        latest: vehicleLive ? logs.adas?.latest || "" : "",
      },
      navigation: {
        active_version: nav.active_version || "v2.1",
        running: String(nav.flag || "ON").toUpperCase() === "ON",
      },
      energy: {
        active_version: energy.active_version || "v2.0",
        running: String(energy.flag || "ON").toUpperCase() === "ON",
        mode: vehicleLive ? logs.energy?.mode || "" : "",
      },
    },
    runtime: {
      car_process_running: procUp,
      probed_at: new Date().toISOString(),
      source: "wsl_pgrep",
    },
    warnings,
  });
});

function readFleetStatesCar1To10(warnings) {
  const states = [];
  for (let i = 1; i <= 10; i += 1) {
    const carId = `car_${i}`;
    const filePath = path.join(DEPLOY_ROOT, "state", `${carId}.json`);
    try {
      if (!fs.existsSync(filePath)) {
        warnings.push(`missing deploy/state/${carId}.json`);
        states.push({
          car_id: carId,
          missing: true,
          model: "",
          region: "",
          group: "",
          status: "unknown",
          features: {},
          ota: {},
        });
        continue;
      }
      states.push(readJson(filePath));
    } catch (err) {
      warnings.push(`invalid deploy/state/${carId}.json: ${err.message}`);
      states.push({
        car_id: carId,
        missing: true,
        model: "",
        region: "",
        group: "",
        status: "unknown",
        features: {},
        ota: {},
      });
    }
  }
  return states;
}

app.get("/api/fleet/state", async (_req, res) => {
  const warnings = [];
  try {
    const states = readFleetStatesCar1To10(warnings);
    const probeMap = await probeFleetCarProcessesWsl(warnings);
    mergeRuntimeProbeIntoStates(states, probeMap, warnings);
    forceOfflineFeatureFlags(states, probeMap, warnings);
    mergeMqttFlagsIntoStates(states, mqttFlagCache, probeMap, warnings);
    mergeLiveEnergyTelemetryIntoStates(states, probeMap, warnings);
    res.json({ ok: true, states, warnings, mqtt_connected: mqttFlagCache.connected });
  } catch (err) {
    warnings.push(`fleet state: ${err.message}`);
    res.json({ ok: false, states: [], warnings });
  }
});

function readFleetHistoryJsonl(limit = 200, warnings) {
  const filePath = path.join(DEPLOY_ROOT, "history", "fleet_history.jsonl");
  if (!fs.existsSync(filePath)) {
    warnings.push("missing deploy/history/fleet_history.jsonl (empty history)");
    return { records: [], skipped_lines: 0, total_lines: 0 };
  }
  const text = readText(filePath);
  const lines = text
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter(Boolean);
  let skipped = 0;
  const all = [];
  for (const line of lines) {
    try {
      all.push(JSON.parse(line));
    } catch (_) {
      skipped += 1;
    }
  }
  if (skipped) {
    warnings.push(`fleet_history.jsonl: skipped ${skipped} non-JSON line(s)`);
  }
  const parsedTotal = all.length;
  const records = all.length <= limit ? all : all.slice(-limit);
  return { records, skipped_lines: skipped, total_lines: lines.length, parsed_total: parsedTotal };
}

app.get("/api/fleet/fleet-history", (req, res) => {
  const warnings = [];
  const lim = Math.min(500, Math.max(1, Number(req.query.limit) || 200));
  const { records, skipped_lines, total_lines, parsed_total } = readFleetHistoryJsonl(lim, warnings);
  res.json({
    ok: true,
    records,
    record_count: records.length,
    skipped_lines,
    total_lines,
    parsed_total,
    file: "deploy/history/fleet_history.jsonl",
    warnings,
  });
});

const ROLLBACK_FEATURES = ["ADAS", "Navigation", "Energy"];

app.get("/api/fleet/rollback-status", (req, res) => {
  const carId = String(req.query.car_id || "").trim();
  if (!/^car_(10|[1-9])$/.test(carId)) {
    res.status(400).json({ ok: false, error: "car_id must be car_1 .. car_10" });
    return;
  }
  const base = path.join(DEPLOY_ROOT, "rollback", carId);
  const payload = { ok: true, car_id: carId };
  for (const feature of ROLLBACK_FEATURES) {
    const fp = path.join(base, `${feature}.json`);
    payload[feature] = fs.existsSync(fp);
  }
  res.json(payload);
});

app.get("/api/fleet/state/:carId", (req, res) => {
  const carId = String(req.params.carId || "");
  const filePath = path.join(DEPLOY_ROOT, "state", `${carId}.json`);
  try {
    const data = readJson(filePath);
    res.json({ ok: true, state: data });
  } catch (err) {
    res.status(404).json({ ok: false, error: err.message, file: filePath });
  }
});

app.post("/api/fleet/driver-approval", async (req, res) => {
  const car_id = String(req.body?.car_id || "");
  const approved = Boolean(req.body?.approved);
  if (!car_id) {
    res.status(400).json({ ok: false, error: "car_id required" });
    return;
  }

  const repoWsl = toWslPath(PLATFORM_DEPLOY_ROOT);
  let dm;
  try {
    dm = detectDeploymentMgrPath();
  } catch (err) {
    res.status(503).json({ ok: false, error: err.message });
    return;
  }
  const cmd =
    `cd ${repoWsl} && ` +
    `${dm.wslPath} fleet-set-approval ${car_id} ${approved ? "true" : "false"}`;

  try {
    const r = await execWslBash(cmd);
    res.json({ ok: true, exit_code: r.code, stdout: r.stdout, stderr: r.stderr });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message, stderr: err.stderr, stdout: err.stdout });
  }
});

function parseSummaryJsonFromStdout(stdout) {
  const text = String(stdout || "");
  const marker = "[SUMMARY_JSON]";
  const idx = text.lastIndexOf(marker);
  if (idx === -1) return null;
  const after = text.slice(idx + marker.length).trim();
  try {
    return JSON.parse(after);
  } catch {
    return null;
  }
}

const FLEET_ACTION_FEATURES = new Set(["ADAS", "Navigation", "Energy"]);
const CAR_ID_RE = /^car_(10|[1-9])$/;

function fleetFeatureToLogBasename(feature) {
  if (feature === "ADAS") return "AdasFeature";
  if (feature === "Navigation") return "NavigationFeature";
  if (feature === "Energy") return "EvEnergyFeature";
  return null;
}

function fleetFeatureToDomainTag(feature) {
  if (feature === "ADAS") return "ADAS";
  if (feature === "Navigation") return "NAV";
  if (feature === "Energy") return "ENERGY";
  return "FEATURE";
}

function primaryLogBaseToShadowBase(primaryBase) {
  if (!primaryBase || primaryBase.endsWith("Shadow")) return null;
  return `${primaryBase}Shadow`;
}

function formatFeatureStyleLogLine(role, domainTag, message) {
  const d = new Date();
  const pad = (n) => String(n).padStart(2, "0");
  const ts = `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(
    d.getMinutes()
  )}:${pad(d.getSeconds())}`;
  return `[${ts}] [INFO ] [${role}][${domainTag}] ${message}`;
}

function collectFleetDeployTargetCars(scope, car_id, stdout) {
  if (scope === "vehicle") {
    const c = String(car_id || "").trim();
    return CAR_ID_RE.test(c) ? [c] : [];
  }
  const j = parseSummaryJsonFromStdout(stdout || "");
  if (j && Array.isArray(j.success_targets)) {
    return j.success_targets.map(String).filter((id) => CAR_ID_RE.test(id));
  }
  return [];
}

/** Same token style as `deploy/state` `active_version` (e.g. v2.0). */
function fleetVersionToken(version) {
  const v = String(version ?? "").trim();
  if (!v) return "v?";
  return v.startsWith("v") ? v : `v${v}`;
}

/** Matches C++ `Feature::maybeLogFleetFeatureDeployBanner` primary-line text (fleet deploy verify). */
function fleetDeployPrimaryLogMessage(feature, version) {
  const token = fleetVersionToken(version);
  if (feature === "ADAS") return `AdasFeature_${token} is deployed!`;
  if (feature === "Navigation") return `NavigationFeature_${token} is deployed!`;
  if (feature === "Energy") return `EvEnergyFeature_${token} is deployed!`;
  return `Fleet deploy completed: ${feature} version ${version}.`;
}

function fleetDeployShadowConsoleLine(primaryVerMsg) {
  const s = String(primaryVerMsg || "");
  if (s.startsWith("AdasFeature_")) return s.replace(/^AdasFeature_/, "AdasFeature_Shadow_");
  if (s.startsWith("NavigationFeature_")) return s.replace(/^NavigationFeature_/, "NavigationFeature_Shadow_");
  if (s.startsWith("EvEnergyFeature_")) return s.replace(/^EvEnergyFeature_/, "EvEnergyFeature_Shadow_");
  return s;
}

function fleetFeatureToStateKey(feature) {
  if (feature === "ADAS") return "ADAS";
  if (feature === "Navigation") return "Navigation";
  if (feature === "Energy") return "Energy";
  return null;
}

function readFleetStateActiveVersion(carId, fleetFeatureLabel) {
  const key = fleetFeatureToStateKey(fleetFeatureLabel);
  if (!key) return null;
  const fp = path.join(DEPLOY_ROOT, "state", `${carId}.json`);
  try {
    const j = readJson(fp);
    const v = j?.features?.[key]?.active_version;
    return typeof v === "string" && v.trim() ? v.trim() : null;
  } catch {
    return null;
  }
}

function readFleetStatePendingVersion(carId, fleetFeatureLabel) {
  const key = fleetFeatureToStateKey(fleetFeatureLabel);
  if (!key) return null;
  const fp = path.join(DEPLOY_ROOT, "state", `${carId}.json`);
  try {
    const j = readJson(fp);
    const v = j?.features?.[key]?.pending_active_version;
    return typeof v === "string" && v.trim() ? v.trim() : null;
  } catch {
    return null;
  }
}

function fleetStateVersionMatchesRequested(stateVersion, requestedToken) {
  if (!stateVersion) return false;
  return fleetVersionToken(stateVersion) === requestedToken;
}

function safeWriteJsonAtomic(filePath, data) {
  const dir = path.dirname(filePath);
  fs.mkdirSync(dir, { recursive: true });
  const tmp = `${filePath}.tmp.${process.pid}.${Date.now()}`;
  fs.writeFileSync(tmp, `${JSON.stringify(data, null, 2)}\n`, { encoding: "utf8" });
  fs.renameSync(tmp, filePath);
}

/**
 * Force-update deploy/state/<car>.json for a fleet deploy so that the UI expectation
 * ("deploy => state file changes") is always satisfied, even if DeploymentMgr returns
 * partial failures or skips updating state for some reason.
 *
 * NOTE: This does not restart any running car processes; it only updates the state JSON.
 */
function forceUpdateFleetState(carId, fleetFeatureLabel, slot, versionToken) {
  if (!CAR_ID_RE.test(carId)) return { ok: false, error: "invalid car_id" };
  const key = fleetFeatureToStateKey(fleetFeatureLabel);
  if (!key) return { ok: false, error: "invalid feature" };
  const sl = String(slot || "").trim().toUpperCase();
  if (sl !== "A" && sl !== "B") return { ok: false, error: "invalid slot" };
  const fp = path.join(DEPLOY_ROOT, "state", `${carId}.json`);
  let j;
  try {
    j = readJson(fp);
  } catch (err) {
    return { ok: false, error: `read failed: ${err.message}` };
  }
  if (!j || typeof j !== "object") return { ok: false, error: "state json invalid" };
  if (!j.features || typeof j.features !== "object") j.features = {};
  if (!j.features[key] || typeof j.features[key] !== "object") j.features[key] = {};
  j.features[key].active_slot = sl;
  j.features[key].active_version = versionToken;
  try {
    safeWriteJsonAtomic(fp, j);
    return { ok: true };
  } catch (err) {
    return { ok: false, error: `write failed: ${err.message}` };
  }
}

function otaDomainToFleetFeature(domain) {
  if (domain === "ADAS") return "ADAS";
  if (domain === "Navigation") return "Navigation";
  if (domain === "EvEnergy") return "Energy";
  return null;
}

/** Matches `StateManager` / `vehicle_runtime`: candidate only when shadow lane is on and versions differ. */
function fleetFeatureShadowMode(shadowEnabled, activeVersion, shadowVersion) {
  if (!shadowEnabled) return "inactive";
  const norm = (x) =>
    String(x ?? "")
      .trim()
      .toLowerCase()
      .replace(/^v(?=\d)/i, "");
  const a = norm(activeVersion);
  const s = norm(shadowVersion);
  if (!s || a === s) return "inactive";
  return "candidate";
}

/**
 * After a successful `ota_vehicle.sh` run, align `platform/deploy/state/<car>.json`
 * (`features.<ADAS|Navigation|Energy>.active_version` / `shadow_version`) with the Docker
 * tag so native `Feature::maybeLogFleetFeatureDeployBanner` and fleet UI match `vehicle_runtime`.
 */
function syncFleetStateAfterDockerRuntime(carId, fleetFeatureLabel, subcommand, version, _syncShadow) {
  if (!CAR_ID_RE.test(carId)) return;
  const key = fleetFeatureToStateKey(fleetFeatureLabel);
  if (!key) return;
  const fp = path.join(DEPLOY_ROOT, "state", `${carId}.json`);
  if (!fs.existsSync(fp)) {
    console.warn(`[docker-ota] fleet state missing, skip sync: ${path.relative(REPO_ROOT, fp)}`);
    return;
  }
  let j;
  try {
    j = readJson(fp);
  } catch (err) {
    console.warn(`[docker-ota] fleet state read failed (${carId}): ${err.message}`);
    return;
  }
  if (!j || typeof j !== "object") return;
  if (!j.features || typeof j.features !== "object") j.features = {};
  if (!j.features[key] || typeof j.features[key] !== "object") j.features[key] = {};
  const f = j.features[key];
  const token = fleetVersionToken(version);
  if (subcommand === "bootstrap") {
    f.active_version = token;
    f.shadow_enabled = false;
    f.shadow_slot = "";
    f.shadow_version = "";
  } else if (subcommand === "deploy-shadow") {
    f.shadow_version = token;
    f.shadow_enabled = true;
    if (!f.shadow_slot) {
      f.shadow_slot = "B";
    }
  } else if (subcommand === "promote") {
    f.active_version = token;
    f.shadow_enabled = false;
    f.shadow_slot = "";
    f.shadow_version = "";
    // `_syncShadow` is applied by ota_vehicle.sh to Docker only; fleet shadow lane is cleared here.
  }
  f.shadow_mode = fleetFeatureShadowMode(
    Boolean(f.shadow_enabled),
    f.active_version,
    f.shadow_version,
  );
  try {
    safeWriteJsonAtomic(fp, j);
  } catch (err) {
    console.warn(`[docker-ota] fleet state write failed (${carId}): ${err.message}`);
  }
}

/**
 * English version/deploy notifications:
 * - logs/<car>/<Feature>.log (C++-style line when feature is known)
 * - logs/<car>/version_events.log (tailed in car CMD when using start_platform_wsl --foreground)
 * - logs/run_sdv/<car>.log (tailed when SDV_CAR_BACKGROUND=1)
 * - process stdout (status-server CMD)
 */
function notifyCarVersionChange(carId, fleetFeatureLabel, opts) {
  const {
    rolePrimary = "DEPLOY",
    messagePrimary,
    alsoShadowLog = false,
    messageShadow,
    plainConsoleLine,
    writeFeatureLog = false,
    alsoShadowEvent = false,
    plainConsoleLineShadow,
  } = opts;
  if (!CAR_ID_RE.test(carId)) {
    return;
  }
  const plain = plainConsoleLine || messagePrimary;
  const iso = new Date().toISOString();
  const bundleLine = `[${iso}] [VERSION] ${carId} ${fleetFeatureLabel} ${plain}`;
  const carLogDir = path.join(REPO_ROOT, "logs", carId);
  const runSdvLog = path.join(REPO_ROOT, "logs", "run_sdv", `${carId}.log`);
  try {
    fs.mkdirSync(carLogDir, { recursive: true });
    fs.appendFileSync(path.join(carLogDir, "version_events.log"), `${bundleLine}\n`, { encoding: "utf8" });
    // Split streams for separate CMD windows
    fs.appendFileSync(path.join(carLogDir, "version_events.primary.log"), `${bundleLine}\n`, { encoding: "utf8" });
    if (alsoShadowEvent) {
      const plainS = plainConsoleLineShadow || messageShadow || messagePrimary;
      const shadowLine = `[${iso}] [VERSION] ${carId} ${fleetFeatureLabel} ${plainS}`;
      fs.appendFileSync(path.join(carLogDir, "version_events.shadow.log"), `${shadowLine}\n`, { encoding: "utf8" });
    }
  } catch (_) {
    /* ignore */
  }
  try {
    fs.mkdirSync(path.dirname(runSdvLog), { recursive: true });
    fs.appendFileSync(runSdvLog, `${bundleLine}\n`, { encoding: "utf8" });
  } catch (_) {
    /* ignore */
  }
  console.log(bundleLine);

  if (!writeFeatureLog) {
    return;
  }
  const base = fleetFeatureToLogBasename(fleetFeatureLabel);
  if (!base) {
    return;
  }
  const dom = fleetFeatureToDomainTag(fleetFeatureLabel);
  const primaryLine = formatFeatureStyleLogLine(rolePrimary, dom, messagePrimary);
  try {
    fs.appendFileSync(path.join(carLogDir, `${base}.log`), `${primaryLine}\n`, { encoding: "utf8" });
  } catch (_) {
    /* ignore */
  }
  if (alsoShadowLog) {
    const shadowBase = primaryLogBaseToShadowBase(base);
    if (shadowBase) {
      const msgS = messageShadow != null ? messageShadow : messagePrimary;
      const shadowLine = formatFeatureStyleLogLine("SHADOW", dom, msgS);
      try {
        fs.appendFileSync(path.join(carLogDir, `${shadowBase}.log`), `${shadowLine}\n`, { encoding: "utf8" });
      } catch (_) {
        /* ignore */
      }
    }
  }
}

app.post("/api/fleet/action", async (req, res) => {
  const action = String(req.body?.action || "").trim();
  const car_id = String(req.body?.car_id || "").trim();
  const feature = String(req.body?.feature || "").trim();

  if (!CAR_ID_RE.test(car_id)) {
    res.status(400).json({ ok: false, error: "car_id must be car_1 .. car_10" });
    return;
  }
  if (!FLEET_ACTION_FEATURES.has(feature)) {
    res.status(400).json({ ok: false, error: "feature must be ADAS, Navigation, or Energy" });
    return;
  }

  const repoWsl = toWslPath(PLATFORM_DEPLOY_ROOT);
  let dm;
  try {
    dm = detectDeploymentMgrPath();
  } catch (err) {
    res.status(503).json({ ok: false, error: err.message });
    return;
  }
  let cmd = `cd ${repoWsl} && ${dm.wslPath} `;

  if (action === "rollback") {
    const sync_config = req.body?.sync_config === true;
    cmd += `fleet-rollback vehicle ${car_id} ${feature}`;
    if (sync_config) cmd += " --sync-config";
  } else if (action === "flag") {
    const flag = String(req.body?.flag || "").trim().toUpperCase();
    if (flag !== "ON" && flag !== "OFF") {
      res.status(400).json({ ok: false, error: "flag must be ON or OFF" });
      return;
    }
    const sync_config = req.body?.sync_config === true;
    cmd += `fleet-flag vehicle ${car_id} ${feature} ${flag}`;
    if (sync_config) cmd += " --sync-config";
  } else if (action === "kill") {
    const reason = String(req.body?.reason || "").trim();
    if (!reason || !/^[a-zA-Z0-9_-]+$/.test(reason)) {
      res.status(400).json({ ok: false, error: "reason is required (single token: [a-zA-Z0-9_-]+)" });
      return;
    }
    const sync_config = req.body?.sync_config === true;
    cmd += `fleet-kill vehicle ${car_id} ${feature} ${reason}`;
    if (sync_config) cmd += " --sync-config";
  } else if (action === "shadow") {
    const shadow_action = String(req.body?.shadow_action || "").trim().toUpperCase();
    if (shadow_action !== "ON" && shadow_action !== "OFF") {
      res.status(400).json({ ok: false, error: "shadow_action must be ON or OFF" });
      return;
    }
    if (req.body?.sync_config === true) {
      res.status(400).json({
        ok: false,
        error: "sync_config is not supported for fleet-shadow (CLI rejects --sync-config)",
      });
      return;
    }
    cmd += `fleet-shadow vehicle ${car_id} ${feature} ${shadow_action}`;
  } else {
    res.status(400).json({ ok: false, error: "action must be rollback, flag, kill, or shadow" });
    return;
  }

  try {
    const r = await execWslBash(cmd);
    const summary_json = parseSummaryJsonFromStdout(r.stdout);
    const okExec = Number(r.code) === 0;
    if (okExec) {
      let msg = "";
      if (action === "rollback") {
        msg = `Fleet rollback completed for ${feature}.`;
      } else if (action === "flag") {
        const flag = String(req.body?.flag || "").trim().toUpperCase();
        msg = `Fleet feature flag set to ${flag} for ${feature}.`;
      } else if (action === "kill") {
        const reason = String(req.body?.reason || "").trim();
        msg = `Fleet kill switch invoked for ${feature} (reason: ${reason}).`;
      } else if (action === "shadow") {
        const shadow_action = String(req.body?.shadow_action || "").trim().toUpperCase();
        msg = `Fleet shadow mode ${shadow_action} for ${feature}.`;
        notifyCarVersionChange(car_id, feature, {
          messagePrimary: msg,
          alsoShadowLog: true,
          messageShadow: `Shadow observer slot: fleet shadow ${shadow_action} for ${feature}.`,
          plainConsoleLine: `fleet-action shadow ${shadow_action} OK`,
          alsoShadowEvent: true,
          plainConsoleLineShadow: `fleet-action shadow ${shadow_action} OK (shadow)`,
        });
      }
      if (action !== "shadow") {
        notifyCarVersionChange(car_id, feature, {
          messagePrimary: msg,
          plainConsoleLine: `fleet-action ${action} OK`,
        });
      }
    }
    res.json({
      ok: okExec,
      action,
      car_id,
      feature,
      exit_code: r.code,
      stdout: r.stdout || "",
      stderr: r.stderr || "",
      summary_json,
      error: okExec ? undefined : (r.stderr?.trim() || `DeploymentMgr exited with code ${r.code}`),
    });
  } catch (err) {
    res.status(500).json({
      ok: false,
      action,
      car_id,
      feature,
      error: err.message,
      stderr: err.stderr,
      stdout: err.stdout,
      summary_json: null,
    });
  }
});

app.post("/api/fleet/deploy", async (req, res) => {
  const scope = String(req.body?.scope || "");
  const feature = String(req.body?.feature || "");
  const slot = String(req.body?.slot || "");
  const version = String(req.body?.version || "");
  const car_id = String(req.body?.car_id || "");
  const group = String(req.body?.group || "");
  const region = String(req.body?.region || "");

  if (!scope || !feature || !slot || !version) {
    res.status(400).json({ ok: false, error: "scope, feature, slot, version required" });
    return;
  }

  const repoWsl = toWslPath(PLATFORM_DEPLOY_ROOT);
  let dm;
  try {
    dm = detectDeploymentMgrPath();
  } catch (err) {
    res.status(503).json({ ok: false, error: err.message });
    return;
  }
  let cmd = `cd ${repoWsl} && ${dm.wslPath} fleet-deploy ${scope} `;
  if (scope === "vehicle") {
    if (!car_id) return res.status(400).json({ ok: false, error: "car_id required" });
    cmd += `${car_id} `;
  } else if (scope === "group") {
    if (!group) return res.status(400).json({ ok: false, error: "group required" });
    cmd += `${group} `;
  } else if (scope === "region") {
    if (!region) return res.status(400).json({ ok: false, error: "region required" });
    cmd += `${region} `;
  } else if (scope === "group_region") {
    if (!group || !region) return res.status(400).json({ ok: false, error: "group and region required" });
    cmd += `${group} ${region} `;
  } else {
    return res.status(400).json({ ok: false, error: "invalid scope (vehicle|group|region|group_region)" });
  }

  cmd += `${feature} ${slot} ${version}`;
  const sync_config = req.body?.sync_config === true;
  if (sync_config) {
    cmd += " --sync-config";
  }

  try {
    const r = await execWslBash(cmd);
    const summary_json = parseSummaryJsonFromStdout(r.stdout);

    // IMPORTANT: DeploymentMgr may return non-zero for partial failures even when some vehicles succeed.
    // So we decide "who to log for" based on summary_json.success_targets when available, and verify
    // success by reading deploy/state/<car>.json (active_version).
    let cars = [];
    if (summary_json && Array.isArray(summary_json.success_targets)) {
      cars = summary_json.success_targets.map(String).filter((id) => CAR_ID_RE.test(id));
    } else {
      // Fallback: for explicit vehicle scope, we can still verify via state file even if exit code is non-zero.
      if (scope === "vehicle" && CAR_ID_RE.test(car_id)) {
        cars = [car_id];
      }
    }

    const requestedToken = fleetVersionToken(version);
    const verMsg = fleetDeployPrimaryLogMessage(feature, version);
    const loggedCars = [];
    const notAppliedCars = [];
    const forcedUpdated = [];
    for (const cid of cars) {
      const activeVer = readFleetStateActiveVersion(cid, feature);
      const pendingVer = readFleetStatePendingVersion(cid, feature);
      const satisfiedActive = fleetStateVersionMatchesRequested(activeVer, requestedToken);
      const satisfiedPending = fleetStateVersionMatchesRequested(pendingVer, requestedToken);

      // Legacy recovery: only rewrite active_slot/active_version when deploy succeeded but state is inconsistent.
      if (Number(r.code) === 0 && !satisfiedActive && !satisfiedPending) {
        const forced = forceUpdateFleetState(cid, feature, slot, requestedToken);
        if (forced.ok) {
          forcedUpdated.push(cid);
        }
      }

      const actualAfter = readFleetStateActiveVersion(cid, feature);
      const pendingAfter = readFleetStatePendingVersion(cid, feature);
      const okActive = fleetStateVersionMatchesRequested(actualAfter, requestedToken);
      const okPending = fleetStateVersionMatchesRequested(pendingAfter, requestedToken);

      if (okActive && actualAfter) {
        notifyCarVersionChange(cid, feature, {
          messagePrimary: verMsg,
          plainConsoleLine: verMsg,
          writeFeatureLog: false,
          alsoShadowEvent: true,
          plainConsoleLineShadow: fleetDeployShadowConsoleLine(verMsg),
        });
        loggedCars.push(cid);
      } else if (okPending && pendingAfter) {
        const sched = `${feature} ${requestedToken} scheduled (stays on ${actualAfter || "previous"} until next car restart / apply-pending-native-car).`;
        notifyCarVersionChange(cid, feature, {
          messagePrimary: sched,
          plainConsoleLine: sched,
          writeFeatureLog: false,
          alsoShadowEvent: true,
          plainConsoleLineShadow: `Shadow observer: ${sched}`,
        });
        loggedCars.push(cid);
      } else {
        const note = `Deploy requested (${requestedToken}) but state not updated (active=${actualAfter || "<missing>"}, pending=${pendingAfter || "<missing>"}).`;
        notifyCarVersionChange(cid, feature, {
          messagePrimary: note,
          plainConsoleLine: note,
          writeFeatureLog: false,
          alsoShadowEvent: true,
          plainConsoleLineShadow: `Shadow observer: ${note}`,
        });
        notAppliedCars.push({
          car_id: cid,
          active_version: actualAfter || "<missing>",
          pending_version: pendingAfter || "<missing>",
        });
      }
    }
    if (!cars.length) {
      console.log(
        `[VERSION] fleet-deploy ${scope} ${feature} ${slot} ${version} (exit_code=${r.code}) (no success_targets in SUMMARY_JSON)`
      );
    }
    if (notAppliedCars.length) {
      console.log(
        `[VERSION] fleet-deploy verify failed for ${feature} ${requestedToken}: ` +
          notAppliedCars
            .map((x) => `${x.car_id} active=${x.active_version} pending=${x.pending_version ?? "<missing>"}`)
            .join(", ")
      );
    }

    // We return raw stdout; UI will parse [SUMMARY_JSON] line. summary_json is included for debugging.
    res.json({
      ok: loggedCars.length > 0,
      exit_code: r.code,
      stdout: r.stdout,
      stderr: r.stderr,
      summary_json,
      success_targets_logged: loggedCars,
      verify_failed: notAppliedCars,
      requested_version: requestedToken,
      state_forced_updated: forcedUpdated,
    });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message, stderr: err.stderr, stdout: err.stdout });
  }
});

function bashSingleQuote(s) {
  return `'${String(s).replace(/'/g, `'\\''`)}'`;
}

function normalizeOtaDomain(raw) {
  const d = String(raw ?? "").trim();
  if (!d) {
    throw new Error("domain required");
  }
  const key = d.replace(/\s+/g, "").toLowerCase();
  const map = {
    adas: "ADAS",
    navigation: "Navigation",
    evenergy: "EvEnergy",
    energy: "EvEnergy",
  };
  if (map[key]) {
    return map[key];
  }
  throw new Error("domain must be ADAS, Navigation, or EvEnergy");
}

function normalizeOtaVehicleVersionString(raw) {
  const s = String(raw ?? "").trim();
  if (!s) return s;
  const c0 = s[0];
  const c1 = s[1];
  if ((c0 === "v" || c0 === "V") && c1 !== undefined && /[0-9]/.test(c1)) {
    return s.slice(1);
  }
  return s;
}

function parseVehicleRuntimeBody(body) {
  const car_id = String(body?.car_id || "").trim();
  if (!RUNTIME_DOCKER_CAR_RE.test(car_id)) {
    throw new Error("car_id must be car_1 .. car_10");
  }
  const domain = normalizeOtaDomain(body?.domain);
  const versionRaw = String(body?.version || "").trim();
  if (!versionRaw) {
    throw new Error("version required");
  }
  const version = normalizeOtaVehicleVersionString(versionRaw);
  if (!version) {
    throw new Error("version required");
  }
  if (version.length > 48 || !/^[A-Za-z0-9][A-Za-z0-9._+~-]*$/.test(version)) {
    throw new Error("invalid version string");
  }
  return { car_id, domain, version, sync_shadow: body?.sync_shadow === true };
}

function buildOtaVehicleShCommand(subcommand, opts) {
  if (!fs.existsSync(OTA_VEHICLE_SCRIPT)) {
    throw new Error(`ota_vehicle.sh not found at ${OTA_VEHICLE_SCRIPT}`);
  }
  const sw = toWslPath(OTA_VEHICLE_SCRIPT);
  let cmd = `bash ${bashSingleQuote(sw)} ${bashSingleQuote(subcommand)} --car ${bashSingleQuote(
    opts.car_id,
  )} --domain ${bashSingleQuote(opts.domain)}`;
  if (opts.version != null && String(opts.version) !== "") {
    cmd += ` --version ${bashSingleQuote(String(opts.version))}`;
  }
  if (opts.sync_shadow && subcommand === "promote") {
    cmd += " --sync-shadow";
  }
  return cmd;
}

function normalizeVehicleRuntimeOperation(raw) {
  const op = String(raw ?? "bootstrap")
    .trim()
    .toLowerCase()
    .replace(/_/g, "-");
  if (op === "bootstrap") return "bootstrap";
  if (op === "deploy-shadow" || op === "deployshadow") return "deploy-shadow";
  if (op === "promote") return "promote";
  throw new Error("operation must be bootstrap, deploy-shadow, or promote");
}

function defaultPendingDockerRollout() {
  return { schema_version: 1, queue: [] };
}

function readPendingDockerRollout() {
  try {
    if (!fs.existsSync(PENDING_DOCKER_ROLLOUT_PATH)) {
      return defaultPendingDockerRollout();
    }
    const raw = fs.readFileSync(PENDING_DOCKER_ROLLOUT_PATH, "utf8").trim();
    if (!raw) {
      const fresh = defaultPendingDockerRollout();
      writePendingDockerRollout(fresh);
      return fresh;
    }
    const data = JSON.parse(raw);
    if (!data || typeof data !== "object" || !Array.isArray(data.queue)) {
      return defaultPendingDockerRollout();
    }
    return data;
  } catch {
    return defaultPendingDockerRollout();
  }
}

function writePendingDockerRollout(data) {
  try {
    if (!fs.existsSync(OTA_VEHICLE_RUNTIME_DIR)) {
      fs.mkdirSync(OTA_VEHICLE_RUNTIME_DIR, { recursive: true });
    }
    fs.writeFileSync(PENDING_DOCKER_ROLLOUT_PATH, `${JSON.stringify(data, null, 2)}\n`, "utf8");
  } catch (err) {
    throw new Error(`failed to write pending docker queue: ${err.message}`);
  }
}

function parseVehicleRuntimeQueueBody(body) {
  const operation = normalizeVehicleRuntimeOperation(body?.operation);
  const t = parseVehicleRuntimeBody(body);
  return { operation, ...t };
}

async function execVehicleRuntimeWsl(subcommand, t) {
  const cmd = buildOtaVehicleShCommand(subcommand, t);
  return execWslBash(cmd);
}

function notifyVehicleRuntimeDockerSuccess(subcommand, t) {
  const ff = otaDomainToFleetFeature(t.domain);
  if (!ff) return;
  syncFleetStateAfterDockerRuntime(t.car_id, ff, subcommand, t.version, t.sync_shadow === true);
  if (subcommand === "bootstrap") {
    notifyCarVersionChange(t.car_id, ff, {
      messagePrimary: `Docker OTA bootstrap: Primary and Shadow image tag v${t.version} (${t.domain}).`,
      alsoShadowLog: true,
      messageShadow: `Docker OTA bootstrap: Shadow image tag v${t.version} (${t.domain}).`,
      plainConsoleLine: `docker-ota bootstrap ${t.domain} v${t.version} OK`,
    });
  } else if (subcommand === "deploy-shadow") {
    notifyCarVersionChange(t.car_id, ff, {
      messagePrimary: `Docker OTA: Shadow-only redeploy to v${t.version} (${t.domain}); Primary unchanged.`,
      alsoShadowLog: true,
      messageShadow: `Docker OTA deploy-shadow: Shadow updated to v${t.version} (${t.domain}).`,
      plainConsoleLine: `docker-ota deploy-shadow ${t.domain} v${t.version} OK`,
    });
  } else if (subcommand === "promote") {
    notifyCarVersionChange(t.car_id, ff, {
      messagePrimary: `Docker OTA promote: Primary updated to v${t.version} (${t.domain}).`,
      alsoShadowLog: Boolean(t.sync_shadow),
      messageShadow: t.sync_shadow
        ? `Docker OTA promote: Shadow synced to v${t.version} (${t.domain}).`
        : undefined,
      plainConsoleLine: `docker-ota promote ${t.domain} v${t.version}${t.sync_shadow ? " +sync-shadow" : ""} OK`,
    });
  }
}

app.get("/api/vehicle-runtime/health", (_req, res) => {
  res.json({
    ok: true,
    script_found: fs.existsSync(OTA_VEHICLE_SCRIPT),
    ota_vehicle_script: path.relative(REPO_ROOT, OTA_VEHICLE_SCRIPT).replace(/\\/g, "/"),
    runtime_state_dir: path.relative(REPO_ROOT, OTA_VEHICLE_RUNTIME_DIR).replace(/\\/g, "/"),
  });
});

app.get("/api/vehicle-runtime/state/:carId", (req, res) => {
  const carId = String(req.params.carId || "").trim();
  if (!RUNTIME_DOCKER_CAR_RE.test(carId)) {
    res.status(400).json({ ok: false, error: "car_id must be car_1 .. car_10" });
    return;
  }
  const fp = path.join(OTA_VEHICLE_RUNTIME_DIR, `${carId}.json`);
  const rel = path.relative(REPO_ROOT, fp).replace(/\\/g, "/");
  if (!fs.existsSync(fp)) {
    res.json({ ok: true, state: null, file: rel });
    return;
  }
  try {
    const data = readJson(fp);
    res.json({ ok: true, state: data, file: rel });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message });
  }
});

app.get("/api/vehicle-runtime/status", async (req, res) => {
  try {
    const car_id = String(req.query.car_id || "").trim();
    if (!RUNTIME_DOCKER_CAR_RE.test(car_id)) {
      res.status(400).json({ ok: false, error: "car_id must be car_1 .. car_10" });
      return;
    }
    const domain = normalizeOtaDomain(req.query.domain);
    const cmd = buildOtaVehicleShCommand("status", { car_id, domain });
    const r = await execWslBash(cmd);
    res.json({
      ok: Number(r.code) === 0,
      exit_code: r.code,
      stdout: r.stdout || "",
      stderr: r.stderr || "",
    });
  } catch (err) {
    const code = err.message && err.message.includes("not found") ? 503 : 400;
    res.status(code).json({ ok: false, error: err.message, stderr: err.stderr, stdout: err.stdout });
  }
});

async function handleVehicleRuntimePost(subcommand, body, res) {
  try {
    const t = parseVehicleRuntimeBody(body);
    const r = await execVehicleRuntimeWsl(subcommand, t);
    const ok = Number(r.code) === 0;
    if (ok) {
      notifyVehicleRuntimeDockerSuccess(subcommand, t);
    }
    res.json({
      ok,
      exit_code: r.code,
      stdout: r.stdout || "",
      stderr: r.stderr || "",
      car_id: t.car_id,
      domain: t.domain,
      version: t.version,
    });
  } catch (err) {
    const msg = err.message || String(err);
    const isMissing = msg.includes("not found");
    const status = isMissing ? 503 : 400;
    res.status(status).json({ ok: false, error: msg, stderr: err.stderr, stdout: err.stdout });
  }
}

app.post("/api/vehicle-runtime/bootstrap", async (req, res) => {
  await handleVehicleRuntimePost("bootstrap", req.body, res);
});

app.post("/api/vehicle-runtime/deploy-shadow", async (req, res) => {
  await handleVehicleRuntimePost("deploy-shadow", req.body, res);
});

app.post("/api/vehicle-runtime/promote", async (req, res) => {
  await handleVehicleRuntimePost("promote", req.body, res);
});

app.get("/api/vehicle-runtime/pending-queue", (_req, res) => {
  try {
    const data = readPendingDockerRollout();
    res.json({
      ok: true,
      queue: data.queue,
      file: path.relative(REPO_ROOT, PENDING_DOCKER_ROLLOUT_PATH).replace(/\\/g, "/"),
    });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message });
  }
});

/** Drop queued Docker ops without running `ota_vehicle.sh`. `mode=all` clears the file queue; `mode=head` removes only the first item (unstick a failed head). */
app.post("/api/vehicle-runtime/clear-pending-queue", (req, res) => {
  try {
    const mode = String(req.body?.mode || "all")
      .trim()
      .toLowerCase();
    const data = readPendingDockerRollout();
    if (!Array.isArray(data.queue)) {
      data.queue = [];
    }
    if (mode === "head") {
      if (data.queue.length) {
        data.queue.shift();
      }
    } else if (mode === "all") {
      data.queue = [];
    } else {
      res.status(400).json({ ok: false, error: 'mode must be "all" or "head"' });
      return;
    }
    writePendingDockerRollout(data);
    res.json({
      ok: true,
      mode,
      queue_length: data.queue.length,
      queue: data.queue,
      file: path.relative(REPO_ROOT, PENDING_DOCKER_ROLLOUT_PATH).replace(/\\/g, "/"),
    });
  } catch (err) {
    const msg = err.message || String(err);
    const isMissing = msg.includes("not found");
    res.status(isMissing ? 503 : 500).json({ ok: false, error: msg });
  }
});

app.post("/api/vehicle-runtime/queue-operation", (req, res) => {
  try {
    const entry = parseVehicleRuntimeQueueBody(req.body);
    const data = readPendingDockerRollout();
    data.queue.push({
      operation: entry.operation,
      car_id: entry.car_id,
      domain: entry.domain,
      version: entry.version,
      sync_shadow: entry.sync_shadow === true,
      queued_at: new Date().toISOString(),
    });
    writePendingDockerRollout(data);
    res.json({
      ok: true,
      queue_length: data.queue.length,
      queue: data.queue,
    });
  } catch (err) {
    const msg = err.message || String(err);
    const isMissing = msg.includes("not found");
    res.status(isMissing ? 503 : 400).json({ ok: false, error: msg });
  }
});

app.post("/api/vehicle-runtime/apply-pending", async (_req, res) => {
  try {
    const data = readPendingDockerRollout();
    const queue = Array.isArray(data.queue) ? [...data.queue] : [];
    const results = [];
    let stopped = false;
    while (queue.length && !stopped) {
      const item = queue[0];
      const sub = item.operation;
      const t = {
        car_id: item.car_id,
        domain: item.domain,
        version: item.version,
        sync_shadow: item.sync_shadow === true,
      };
      const r = await execVehicleRuntimeWsl(sub, t);
      const ok = Number(r.code) === 0;
      results.push({
        operation: sub,
        car_id: t.car_id,
        domain: t.domain,
        version: t.version,
        ok,
        exit_code: r.code,
        stderr: r.stderr || "",
        stdout: r.stdout || "",
      });
      if (ok) {
        notifyVehicleRuntimeDockerSuccess(sub, t);
        queue.shift();
      } else {
        stopped = true;
      }
    }
    data.queue = queue;
    writePendingDockerRollout(data);
    res.json({
      ok: !stopped,
      results,
      pending_remaining: queue.length,
      queue: data.queue,
      note: stopped
        ? "First failure stopped the drain; fix the error and use Apply again for the rest."
        : undefined,
    });
  } catch (err) {
    const msg = err.message || String(err);
    const isMissing = msg.includes("not found");
    res.status(isMissing ? 503 : 500).json({ ok: false, error: msg, stderr: err.stderr, stdout: err.stdout });
  }
});

const FLEET_DOCKER_FEATURE_KEYS = ["ADAS", "Navigation", "Energy"];
const FLEET_DOCKER_ALL_CARS = Array.from({ length: 10 }, (_, i) => `car_${i + 1}`);

function defaultFleetDockerVersions() {
  const cars = {};
  for (const id of FLEET_DOCKER_ALL_CARS) {
    cars[id] = { ADAS: "1.0", Navigation: "1.0", Energy: "1.0" };
  }
  return {
    schema_version: 1,
    defaults: { ADAS: "1.0", Navigation: "1.0", Energy: "1.0" },
    cars,
  };
}

function validateFleetDockerVersionsBody(obj) {
  if (!obj || typeof obj !== "object") {
    return "body must be an object";
  }
  if (Number(obj.schema_version) !== 1) {
    return "schema_version must be 1";
  }
  const defs = obj.defaults;
  if (!defs || typeof defs !== "object") {
    return "defaults must be an object";
  }
  for (const k of FLEET_DOCKER_FEATURE_KEYS) {
    if (!String(defs[k] ?? "").trim()) {
      return `defaults.${k} is required`;
    }
  }
  const cars = obj.cars;
  if (!cars || typeof cars !== "object") {
    return "cars must be an object";
  }
  for (const carId of FLEET_DOCKER_ALL_CARS) {
    const row = cars[carId];
    if (!row || typeof row !== "object") {
      return `cars.${carId} must be an object`;
    }
    for (const k of FLEET_DOCKER_FEATURE_KEYS) {
      if (!String(row[k] ?? "").trim()) {
        return `cars.${carId}.${k} is required`;
      }
    }
  }
  for (const carId of Object.keys(cars)) {
    if (!FLEET_DOCKER_ALL_CARS.includes(carId)) {
      return `unknown car id: ${carId}`;
    }
  }
  return "";
}

function readFleetDockerVersionsDisk() {
  try {
    if (!fs.existsSync(FLEET_DOCKER_VERSIONS_PATH)) {
      return defaultFleetDockerVersions();
    }
    return readJson(FLEET_DOCKER_VERSIONS_PATH);
  } catch (err) {
    return defaultFleetDockerVersions();
  }
}

app.get("/api/fleet/docker-feature-versions", (_req, res) => {
  const warnings = [];
  try {
    const data = readFleetDockerVersionsDisk();
    const err = validateFleetDockerVersionsBody(data);
    if (err) {
      warnings.push(`fleet_feature_versions.json invalid (${err}); returning defaults`);
      return res.json({
        ok: true,
        data: defaultFleetDockerVersions(),
        warnings,
        file: path.relative(REPO_ROOT, FLEET_DOCKER_VERSIONS_PATH).replace(/\\/g, "/"),
      });
    }
    res.json({
      ok: true,
      data,
      warnings,
      file: path.relative(REPO_ROOT, FLEET_DOCKER_VERSIONS_PATH).replace(/\\/g, "/"),
    });
  } catch (err) {
    warnings.push(String(err.message || err));
    res.json({
      ok: true,
      data: defaultFleetDockerVersions(),
      warnings,
      file: path.relative(REPO_ROOT, FLEET_DOCKER_VERSIONS_PATH).replace(/\\/g, "/"),
    });
  }
});

app.post("/api/fleet/docker-feature-versions", (req, res) => {
  const body = req.body || {};
  const err = validateFleetDockerVersionsBody(body);
  if (err) {
    res.status(400).json({ ok: false, error: err });
    return;
  }
  try {
    fs.mkdirSync(path.dirname(FLEET_DOCKER_VERSIONS_PATH), { recursive: true });
    fs.writeFileSync(FLEET_DOCKER_VERSIONS_PATH, `${JSON.stringify(body, null, 2)}\n`, "utf8");
    res.json({
      ok: true,
      file: path.relative(REPO_ROOT, FLEET_DOCKER_VERSIONS_PATH).replace(/\\/g, "/"),
    });
  } catch (e) {
    res.status(500).json({ ok: false, error: e.message || String(e) });
  }
});

app.post("/api/fleet/docker-feature-versions/apply", async (req, res) => {
  const dry = req.query?.dry_run === "1" || req.query?.dry_run === "true";
  try {
    if (!fs.existsSync(APPLY_FLEET_DOCKER_PY)) {
      res.status(503).json({ ok: false, error: `missing ${path.relative(REPO_ROOT, APPLY_FLEET_DOCKER_PY)}` });
      return;
    }
    if (!fs.existsSync(OTA_VEHICLE_SCRIPT)) {
      res.status(503).json({ ok: false, error: "ota_vehicle.sh not found" });
      return;
    }
    const otaWsl = toWslPath(OTA_MANAGER_ROOT);
    const pyWsl = toWslPath(APPLY_FLEET_DOCKER_PY);
    const cfgWsl = toWslPath(FLEET_DOCKER_VERSIONS_PATH);
    let cmd = `cd ${bashSingleQuote(otaWsl)} && python3 ${bashSingleQuote(pyWsl)} --config ${bashSingleQuote(cfgWsl)}`;
    if (dry) {
      cmd += " --dry-run";
    }
    const r = await execWslBash(cmd);
    const ok = Number(r.code) === 0;
    res.json({
      ok,
      exit_code: r.code,
      stdout: r.stdout || "",
      stderr: r.stderr || "",
      dry_run: dry,
    });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message || String(err) });
  }
});

const SDV_MQTT_BROKER_URI = String(process.env.SDV_MQTT_BROKER_URI || "mqtt://127.0.0.1:1884").trim();
mqttFlagCache.start(SDV_MQTT_BROKER_URI);

app.listen(PORT, () => {
  console.log(`[STATUS] server started on http://localhost:${PORT}`);
  console.log(`[STATUS] reading deployment state from ${DEPLOY_ROOT}`);
  console.log(`[STATUS] MQTT flag mirror: ${SDV_MQTT_BROKER_URI} (topic /sdv/vehicle/+/flags/state)`);
});
