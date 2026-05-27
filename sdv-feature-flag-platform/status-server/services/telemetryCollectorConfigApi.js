'use strict';

const fs = require('fs');
const path = require('path');

const BATCH_MIN = 1;
const BATCH_MAX = 100;
const FLUSH_MIN = 500;
const FLUSH_MAX = 2000;
const PUBLISH_MIN = 200;
const PUBLISH_MAX = 30000;
const RUNTIME_CAR_RE = /^car_(10|[1-9])$/;
const PUBLISH_FEATURES = ['AdasFeature', 'NavigationFeature', 'EvEnergyFeature'];

function clampBatch(n) {
  const v = Number(n);
  if (!Number.isFinite(v)) return 50;
  return Math.min(BATCH_MAX, Math.max(BATCH_MIN, Math.trunc(v)));
}

function clampFlush(n) {
  const v = Number(n);
  if (!Number.isFinite(v)) return 1000;
  return Math.min(FLUSH_MAX, Math.max(FLUSH_MIN, Math.trunc(v)));
}

function clampPublish(n) {
  const v = Number(n);
  if (!Number.isFinite(v)) return 1000;
  return Math.min(PUBLISH_MAX, Math.max(PUBLISH_MIN, Math.trunc(v)));
}

function runtimeConfigPath(repoRoot, carId) {
  return path.join(repoRoot, 'config', 'telemetry', 'runtime', `${carId}.json`);
}

function readRuntimeJson(fp) {
  if (!fs.existsSync(fp)) {
    return {
      batch_max: 50,
      flush_ms: 1000,
      publish_default_ms: 1000,
      features: {},
      source: 'default',
    };
  }
  const raw = JSON.parse(fs.readFileSync(fp, 'utf8'));
  const features = {};
  if (raw.features && typeof raw.features === 'object') {
    for (const [name, cfg] of Object.entries(raw.features)) {
      if (cfg && typeof cfg.publish_interval_ms === 'number') {
        features[name] = { publish_interval_ms: clampPublish(cfg.publish_interval_ms) };
      }
    }
  }
  return {
    batch_max: clampBatch(raw.batch_max ?? 50),
    flush_ms: clampFlush(raw.flush_ms ?? 1000),
    publish_default_ms: clampPublish(raw.publish_default_ms ?? 1000),
    features,
    updated_at: raw.updated_at || null,
    source: 'file',
  };
}

function writeRuntimeJson(fp, payload) {
  const dir = path.dirname(fp);
  fs.mkdirSync(dir, { recursive: true });
  let prev = {};
  if (fs.existsSync(fp)) {
    try {
      prev = JSON.parse(fs.readFileSync(fp, 'utf8'));
    } catch {
      prev = {};
    }
  }
  const features = { ...(prev.features || {}) };
  if (payload.features && typeof payload.features === 'object') {
    for (const [name, cfg] of Object.entries(payload.features)) {
      if (cfg && cfg.publish_interval_ms != null) {
        features[name] = { publish_interval_ms: clampPublish(cfg.publish_interval_ms) };
      }
    }
  }
  const body = {
    batch_max: payload.batch_max != null ? clampBatch(payload.batch_max) : clampBatch(prev.batch_max ?? 50),
    flush_ms: payload.flush_ms != null ? clampFlush(payload.flush_ms) : clampFlush(prev.flush_ms ?? 1000),
    publish_default_ms:
      payload.publish_default_ms != null
        ? clampPublish(payload.publish_default_ms)
        : clampPublish(prev.publish_default_ms ?? 1000),
    features,
    updated_at: new Date().toISOString(),
  };
  fs.writeFileSync(fp, `${JSON.stringify(body, null, 2)}\n`, 'utf8');
  return body;
}

function buildFeaturePublishView(runtime) {
  const featurePublish = {};
  for (const name of PUBLISH_FEATURES) {
    featurePublish[name] =
      runtime.features?.[name]?.publish_interval_ms ?? runtime.publish_default_ms;
  }
  return featurePublish;
}

/**
 * @param {import('express').Express} app
 * @param {{ repoRoot: string }} opts
 */
function registerTelemetryCollectorConfigRoutes(app, opts) {
  const repoRoot = opts.repoRoot;

  app.get('/api/telemetry/collector-config/:carId', (req, res) => {
    const carId = String(req.params.carId || '').trim();
    if (!RUNTIME_CAR_RE.test(carId)) {
      res.status(400).json({ ok: false, error: 'car_id must be car_1 .. car_10' });
      return;
    }
    try {
      const rtFp = runtimeConfigPath(repoRoot, carId);
      const rtRel = path.relative(repoRoot, rtFp).replace(/\\/g, '/');
      const runtime = readRuntimeJson(rtFp);
      res.json({
        ok: true,
        car_id: carId,
        batch_max: runtime.batch_max,
        flush_ms: runtime.flush_ms,
        publish_default_ms: runtime.publish_default_ms,
        feature_publish_ms: buildFeaturePublishView(runtime),
        runtime_source: runtime.source,
        runtime_path: rtRel,
        limits: {
          batch_min: BATCH_MIN,
          batch_max: BATCH_MAX,
          flush_min: FLUSH_MIN,
          flush_max: FLUSH_MAX,
          publish_min: PUBLISH_MIN,
          publish_max: PUBLISH_MAX,
        },
        note: 'Telemetry timing is config/telemetry/runtime/<car>.json only. Features and DataCollector reload without car restart.',
      });
    } catch (err) {
      res.status(500).json({ ok: false, error: err.message });
    }
  });

  app.put('/api/telemetry/collector-config/:carId', (req, res) => {
    const carId = String(req.params.carId || '').trim();
    if (!RUNTIME_CAR_RE.test(carId)) {
      res.status(400).json({ ok: false, error: 'car_id must be car_1 .. car_10' });
      return;
    }
    const body = req.body && typeof req.body === 'object' ? req.body : {};
    if (
      body.batch_max == null &&
      body.flush_ms == null &&
      body.publish_default_ms == null &&
      body.features == null &&
      body.feature_publish_ms == null
    ) {
      res.status(400).json({
        ok: false,
        error: 'batch_max, flush_ms, publish_default_ms, features, and/or feature_publish_ms required',
      });
      return;
    }
    try {
      const rtFp = runtimeConfigPath(repoRoot, carId);
      const prev = readRuntimeJson(rtFp);
      const features = { ...prev.features };
      if (body.feature_publish_ms && typeof body.feature_publish_ms === 'object') {
        for (const [name, ms] of Object.entries(body.feature_publish_ms)) {
          if (PUBLISH_FEATURES.includes(name)) {
            features[name] = { publish_interval_ms: clampPublish(ms) };
          }
        }
      }
      if (body.features && typeof body.features === 'object') {
        Object.assign(features, body.features);
      }
      const saved = writeRuntimeJson(rtFp, {
        batch_max: body.batch_max != null ? body.batch_max : prev.batch_max,
        flush_ms: body.flush_ms != null ? body.flush_ms : prev.flush_ms,
        publish_default_ms:
          body.publish_default_ms != null ? body.publish_default_ms : prev.publish_default_ms,
        features,
      });
      const rtRel = path.relative(repoRoot, rtFp).replace(/\\/g, '/');
      res.json({
        ok: true,
        car_id: carId,
        batch_max: saved.batch_max,
        flush_ms: saved.flush_ms,
        publish_default_ms: saved.publish_default_ms,
        features: saved.features,
        updated_at: saved.updated_at,
        runtime_path: rtRel,
        applied: 'runtime_json',
        message: 'Saved. Features and DataCollector pick up on the next cycle (no car restart).',
      });
    } catch (err) {
      res.status(500).json({ ok: false, error: err.message });
    }
  });
}

module.exports = { registerTelemetryCollectorConfigRoutes, clampBatch, clampFlush, clampPublish };
