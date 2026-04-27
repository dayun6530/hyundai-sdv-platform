const express = require("express");
const cors = require("cors");
const fs = require("fs");
const path = require("path");
const { execFile } = require("child_process");

const app = express();
const PORT = Number(process.env.PORT || 8080);
const REPO_ROOT = process.env.SDV_ROOT || path.resolve(__dirname, "..");
const DEPLOY_ROOT = path.join(REPO_ROOT, "deploy");
const DOMAINS = ["ADAS", "NAV", "ENERGY"];

app.use(cors());
app.use(express.json({ limit: "256kb" }));

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
  return new Promise((resolve, reject) => {
    execFile(
      "wsl.exe",
      ["-d", "Ubuntu", "--", "bash", "-lc", args],
      { windowsHide: true, maxBuffer: 5 * 1024 * 1024, ...options },
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
  const unleash = path.join(REPO_ROOT, "build_unleash", "DeploymentMgr");
  const normal = path.join(REPO_ROOT, "build", "DeploymentMgr");
  if (fs.existsSync(unleash)) return { winPath: unleash, wslPath: toWslPath(unleash) };
  return { winPath: normal, wslPath: toWslPath(normal) };
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
  const filePath = path.join(REPO_ROOT, "deploy", "version_map.json");
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

const PACKAGE_REGISTRY_PATH = path.join(REPO_ROOT, "deploy", "package_registry.json");
const PKG_FEATURES = new Set(["ADAS", "Navigation", "Energy"]);
const PKG_ROLES = new Set(["candidate", "stable", "experimental"]);

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

  const package_path = `deploy/packages/${feature}/${version}`;
  const manifest_path = `${package_path}/manifest.json`;

  const manifest = {
    package_id,
    feature,
    version,
    role,
    binary,
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

  const dirAbs = path.join(REPO_ROOT, "deploy", "packages", feature, version);
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
  const vmPath = path.join(REPO_ROOT, "deploy", "version_map.json");
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
  const package_path = String(pkg.package_path || "").trim() || `deploy/packages/${feature}/${version}`;
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

function readFleetStatesCar1To10(warnings) {
  const states = [];
  for (let i = 1; i <= 10; i += 1) {
    const carId = `car_${i}`;
    const filePath = path.join(REPO_ROOT, "deploy", "state", `${carId}.json`);
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

app.get("/api/fleet/state", (_req, res) => {
  const warnings = [];
  try {
    const states = readFleetStatesCar1To10(warnings);
    res.json({ ok: true, states, warnings });
  } catch (err) {
    warnings.push(`fleet state: ${err.message}`);
    res.json({ ok: false, states: [], warnings });
  }
});

function readFleetHistoryJsonl(limit = 200, warnings) {
  const filePath = path.join(REPO_ROOT, "deploy", "history", "fleet_history.jsonl");
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
  const base = path.join(REPO_ROOT, "deploy", "rollback", carId);
  const payload = { ok: true, car_id: carId };
  for (const feature of ROLLBACK_FEATURES) {
    const fp = path.join(base, `${feature}.json`);
    payload[feature] = fs.existsSync(fp);
  }
  res.json(payload);
});

app.get("/api/fleet/state/:carId", (req, res) => {
  const carId = String(req.params.carId || "");
  const filePath = path.join(REPO_ROOT, "deploy", "state", `${carId}.json`);
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

  const repoWsl = toWslPath(REPO_ROOT);
  const dm = detectDeploymentMgrPath();
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

  const repoWsl = toWslPath(REPO_ROOT);
  const dm = detectDeploymentMgrPath();
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

  const repoWsl = toWslPath(REPO_ROOT);
  const dm = detectDeploymentMgrPath();
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
    // We return raw stdout; UI will parse [SUMMARY_JSON] line.
    res.json({ ok: true, exit_code: r.code, stdout: r.stdout, stderr: r.stderr });
  } catch (err) {
    res.status(500).json({ ok: false, error: err.message, stderr: err.stderr, stdout: err.stdout });
  }
});

app.listen(PORT, () => {
  console.log(`[STATUS] server started on http://localhost:${PORT}`);
  console.log(`[STATUS] reading deployment state from ${DEPLOY_ROOT}`);
});
