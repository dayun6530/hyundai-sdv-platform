const { execFile } = require("child_process");
const fs = require("fs");
const path = require("path");

const SUPPORTED_COMMAND_PREVIEW_ACTIONS = Object.freeze([
  "deploy-candidate",
  "run-shadow",
  "promote-candidate",
  "rollback-primary",
  "activate-kill-switch",
  "clear-kill-switch",
]);

function normalizePreviewBody(body = {}) {
  return {
    action: String(body.action || "").trim(),
    vehicle_id: String(body.vehicle_id || body.car_id || "").trim(),
    feature: String(body.feature || "").trim(),
    version: String(body.version || "").trim(),
    reason: String(body.reason || "").trim(),
  };
}

function validateCommandPreviewBody(body = {}) {
  const normalized = normalizePreviewBody(body);
  const { action, vehicle_id, feature, version, reason } = normalized;

  if (!action) return { ok: false, error: "action is required", normalized };
  if (!SUPPORTED_COMMAND_PREVIEW_ACTIONS.includes(action)) {
    return { ok: false, error: `unsupported action: ${action}`, normalized };
  }
  if (!vehicle_id) return { ok: false, error: "vehicle_id is required", normalized };
  if (!feature) return { ok: false, error: "feature is required", normalized };
  if ((action === "deploy-candidate" || action === "run-shadow") && !version) {
    return { ok: false, error: `version is required for action ${action}`, normalized };
  }
  if (action === "activate-kill-switch" && !reason) {
    return { ok: false, error: "reason is required for action activate-kill-switch", normalized };
  }

  return { ok: true, normalized };
}

function execFilePromise(command, args, options) {
  return new Promise((resolve, reject) => {
    execFile(command, args, options, (error, stdout, stderr) => {
      if (error) {
        error.stdout = stdout;
        error.stderr = stderr;
        reject(error);
        return;
      }
      resolve({ stdout, stderr });
    });
  });
}

async function previewDeploymentCommand({ body, projectRoot, pythonCommand = process.env.PYTHON || "python" }) {
  const validation = validateCommandPreviewBody(body);
  if (!validation.ok) {
    const err = new Error(validation.error);
    err.statusCode = 400;
    err.supported_actions = SUPPORTED_COMMAND_PREVIEW_ACTIONS;
    throw err;
  }

  const normalized = validation.normalized;
  const root = path.resolve(projectRoot || ".");
  const adapterPath = path.join(
    root,
    "control-plane",
    "feature-deployment-service",
    "deployment-command-api",
    "adapter",
    "deployment_command_adapter.py",
  );

  if (!fs.existsSync(adapterPath)) {
    const err = new Error(`command preview adapter not found: ${adapterPath}`);
    err.statusCode = 503;
    err.supported_actions = SUPPORTED_COMMAND_PREVIEW_ACTIONS;
    throw err;
  }

  const args = [
    adapterPath,
    "--root",
    root,
    "--action",
    normalized.action,
    "--vehicle",
    normalized.vehicle_id,
    "--feature",
    normalized.feature,
  ];

  if (normalized.version) {
    args.push("--version", normalized.version);
  }
  if (normalized.reason) {
    args.push("--reason", normalized.reason);
  }

  let result;
  try {
    result = await execFilePromise(pythonCommand, args, {
      cwd: root,
      windowsHide: true,
      timeout: 10_000,
      maxBuffer: 1024 * 1024,
    });
  } catch (error) {
    const err = new Error(`command preview adapter failed: ${error.message}`);
    err.statusCode = 502;
    err.stdout = error.stdout || "";
    err.stderr = error.stderr || "";
    err.supported_actions = SUPPORTED_COMMAND_PREVIEW_ACTIONS;
    throw err;
  }

  let preview;
  try {
    preview = JSON.parse(String(result.stdout || ""));
  } catch (error) {
    const err = new Error(`command preview adapter returned non-JSON output: ${error.message}`);
    err.statusCode = 502;
    err.stdout = result.stdout || "";
    err.stderr = result.stderr || "";
    err.supported_actions = SUPPORTED_COMMAND_PREVIEW_ACTIONS;
    throw err;
  }

  if (preview.dry_run !== true || preview.will_execute !== false) {
    const err = new Error("command preview adapter response is missing dry-run safety markers");
    err.statusCode = 502;
    err.preview = preview;
    err.supported_actions = SUPPORTED_COMMAND_PREVIEW_ACTIONS;
    throw err;
  }

  return {
    success: true,
    preview,
    adapter: path.relative(root, adapterPath).replace(/\\/g, "/"),
    stderr: result.stderr || "",
  };
}

module.exports = {
  SUPPORTED_COMMAND_PREVIEW_ACTIONS,
  normalizePreviewBody,
  validateCommandPreviewBody,
  previewDeploymentCommand,
};
