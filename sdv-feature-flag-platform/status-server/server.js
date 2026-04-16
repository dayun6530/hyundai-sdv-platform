const express = require("express");
const cors = require("cors");
const fs = require("fs");
const path = require("path");

const app = express();
const PORT = Number(process.env.PORT || 8080);
const REPO_ROOT = process.env.SDV_ROOT || path.resolve(__dirname, "..");
const DEPLOY_ROOT = path.join(REPO_ROOT, "deploy");
const DOMAINS = ["ADAS", "NAV", "ENERGY"];

app.use(cors());

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

app.listen(PORT, () => {
  console.log(`[STATUS] server started on http://localhost:${PORT}`);
  console.log(`[STATUS] reading deployment state from ${DEPLOY_ROOT}`);
});
