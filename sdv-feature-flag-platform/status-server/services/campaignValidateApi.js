const fs = require("fs");
const path = require("path");

const SAFE_ID_RE = /^[a-zA-Z0-9._-]+$/;
const VALIDATE_TIMEOUT_MS = 90_000;

function validateId(id, label = "id") {
  const s = String(id || "").trim();
  if (!s || !SAFE_ID_RE.test(s)) {
    const err = new Error(`invalid ${label}: must match ${SAFE_ID_RE}`);
    err.statusCode = 400;
    throw err;
  }
  return s;
}

function listJsonFiles(dirPath) {
  if (!fs.existsSync(dirPath)) return [];
  return fs
    .readdirSync(dirPath, { withFileTypes: true })
    .filter((d) => d.isFile() && d.name.endsWith(".json"))
    .map((d) => d.name);
}

function safeReadJson(filePath) {
  try {
    return { ok: true, data: JSON.parse(fs.readFileSync(filePath, "utf8")) };
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

function findCampaignByCampaignId(campaignsDir, campaignId) {
  for (const fileName of listJsonFiles(campaignsDir)) {
    if (!SAFE_ID_RE.test(fileName.replace(/\.json$/, ""))) continue;
    const fp = path.join(campaignsDir, fileName);
    const base = path.resolve(campaignsDir);
    const resolved = path.resolve(fp);
    if (resolved !== base && !resolved.startsWith(base + path.sep)) continue;
    const { ok, data } = safeReadJson(resolved);
    if (ok && String(data?.campaign_id || "") === campaignId) {
      return { fileName, data, campaignPath: `deploy/campaigns/${fileName}` };
    }
  }
  return null;
}

function parseCampaignValidateOutput(stdout, stderr) {
  const items = [];
  let summary = { ok: 0, warn: 0, fail: 0 };
  let campaignIdFromLine = "";

  const lines = `${stdout || ""}\n${stderr || ""}`.split(/\r?\n/);
  for (const raw of lines) {
    const line = raw.trim();
    if (!line) continue;

    const cv = line.match(/^\[CAMPAIGN_VALIDATE\]\s+campaign_id=(\S+)\s+status=(\S+)/);
    if (cv) {
      campaignIdFromLine = cv[1];
      continue;
    }

    const sum = line.match(/^\[SUMMARY\]\s+ok=(\d+)\s+warn=(\d+)\s+fail=(\d+)/);
    if (sum) {
      summary = { ok: Number(sum[1]), warn: Number(sum[2]), fail: Number(sum[3]) };
      continue;
    }

    const okM = line.match(/^\[OK\]\s*(.*)$/);
    const warnM = line.match(/^\[WARN\]\s*(.*)$/);
    const failM = line.match(/^\[FAIL\]\s*(.*)$/);
    if (okM) items.push({ level: "OK", message: okM[1].trim() });
    else if (warnM) items.push({ level: "WARN", message: warnM[1].trim() });
    else if (failM) items.push({ level: "FAIL", message: failM[1].trim() });
  }

  if (!summary.ok && !summary.warn && !summary.fail && items.length) {
    summary = {
      ok: items.filter((i) => i.level === "OK").length,
      warn: items.filter((i) => i.level === "WARN").length,
      fail: items.filter((i) => i.level === "FAIL").length,
    };
  }

  let status = "OK";
  if (summary.fail > 0) status = "FAIL";
  else if (summary.warn > 0) status = "OK with WARN";

  return { summary, items, status, campaign_id: campaignIdFromLine };
}

function registerCampaignValidateRoutes(app, deps) {
  const {
    DEPLOY_ROOT,
    PLATFORM_DEPLOY_ROOT,
    execWslBash,
    detectDeploymentMgrPath,
    toWslPath,
    bashSingleQuote,
  } = deps;

  const CAMPAIGNS_DIR = path.join(DEPLOY_ROOT, "campaigns");

  app.post("/api/campaigns/:campaignId/validate", async (req, res) => {
    try {
      const campaignId = validateId(req.params.campaignId, "campaignId");
      const hit = findCampaignByCampaignId(CAMPAIGNS_DIR, campaignId);
      if (!hit) {
        res.status(404).json({ ok: false, error: `campaign not found: ${campaignId}` });
        return;
      }

      let dm;
      try {
        dm = detectDeploymentMgrPath();
      } catch (err) {
        res.status(503).json({ ok: false, error: err.message, campaign_id: campaignId });
        return;
      }

      const repoWsl = toWslPath(PLATFORM_DEPLOY_ROOT);
      const relPath = hit.campaignPath;
      const cmd =
        `cd ${bashSingleQuote(repoWsl)} && ` +
        `${bashSingleQuote(dm.wslPath)} campaign-validate ${bashSingleQuote(relPath)}`;

      const r = await execWslBash(cmd, { timeout: VALIDATE_TIMEOUT_MS });
      const parsed = parseCampaignValidateOutput(r.stdout, r.stderr);
      const exitOk = Number(r.code) === 0;

      res.json({
        ok: exitOk,
        campaign_id: campaignId,
        status: parsed.status,
        summary: parsed.summary,
        items: parsed.items,
        stdout: r.stdout || "",
        stderr: r.stderr || "",
        exit_code: r.code,
        campaign_file: hit.fileName,
        validate_command: `DeploymentMgr campaign-validate ${relPath}`,
      });
    } catch (err) {
      const code = err.statusCode || 500;
      res.status(code).json({
        ok: false,
        error: err.message || String(err),
        status: "FAIL",
        summary: { ok: 0, warn: 0, fail: 1 },
        items: [{ level: "FAIL", message: err.message || String(err) }],
      });
    }
  });
}

module.exports = {
  registerCampaignValidateRoutes,
  parseCampaignValidateOutput,
  findCampaignByCampaignId,
  validateId,
};
