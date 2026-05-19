"use strict";

const express = require("express");
const { randomUUID } = require("crypto");
const { Pool } = require("pg");

const PORT = Number(process.env.PORT || 8090);
const DATABASE_URL =
  process.env.DATABASE_URL ||
  "postgres://sdv:sdv@localhost:5435/sdv_operational";
const MAX_BATCH_SIZE = Math.min(
  Math.max(Number(process.env.SDV_INGEST_BATCH_MAX || 100), 1),
  100,
);

const pool = new Pool({
  connectionString: DATABASE_URL,
  max: 20,
  idleTimeoutMillis: 30_000,
});

const app = express();
app.use(express.json({ limit: "2mb" }));

function parseOccurredAt(value) {
  if (!value || typeof value !== "string") {
    return null;
  }
  const d = new Date(value);
  return Number.isNaN(d.getTime()) ? null : d.toISOString();
}

function normalizeIngestBody(body) {
  if (!body || typeof body !== "object") {
    return { error: "JSON body required" };
  }
  const vehicleId = String(body.vehicle_id || body.car_id || "").trim();
  if (!vehicleId) {
    return { error: "vehicle_id (or car_id) required" };
  }
  const eventType = String(body.event_type || "telemetry").trim() || "telemetry";
  const feature = body.feature != null ? String(body.feature).trim() : null;
  const level = body.level != null ? String(body.level).trim() : null;
  const role = body.role != null ? String(body.role).trim() : null;
  const occurredAt = parseOccurredAt(body.occurred_at || body.timestamp);

  let payload = body.payload ?? null;
  if (payload == null && body.raw != null && typeof body.raw === "object" && body.raw.payload != null) {
    payload = body.raw.payload;
  }
  if (payload == null) {
    payload = body.raw ?? null;
  }
  if (payload != null && typeof payload === "string") {
    try {
      payload = JSON.parse(payload);
    } catch {
      payload = { text: payload };
    }
  }

  let rawJson = body.raw ?? body.raw_json ?? null;
  if (rawJson != null && typeof rawJson === "string") {
    try {
      rawJson = JSON.parse(rawJson);
    } catch {
      rawJson = { text: rawJson };
    }
  }
  if (rawJson == null && body.rawJson != null) {
    rawJson = body.rawJson;
  }

  return {
    vehicleId,
    feature,
    eventType,
    level,
    role,
    occurredAt,
    payload,
    rawJson,
  };
}

async function insertOperationalEvent(client, norm, ingestBatchId) {
  const params = [
    norm.vehicleId,
    norm.feature,
    norm.eventType,
    norm.level,
    norm.role,
    norm.occurredAt,
    norm.payload != null ? JSON.stringify(norm.payload) : null,
    norm.rawJson != null ? JSON.stringify(norm.rawJson) : null,
  ];
  let sql = `INSERT INTO operational_events
         (vehicle_id, feature, event_type, level, role, occurred_at, payload, raw_json`;
  if (ingestBatchId) {
    sql += `, ingest_batch_id) VALUES ($1, $2, $3, $4, $5, $6::timestamptz, $7::jsonb, $8::jsonb, $9::uuid)
       RETURNING id, received_at`;
    params.push(ingestBatchId);
  } else {
    sql += `) VALUES ($1, $2, $3, $4, $5, $6::timestamptz, $7::jsonb, $8::jsonb)
       RETURNING id, received_at`;
  }
  return client.query(sql, params);
}

app.get("/health", async (_req, res) => {
  try {
    await pool.query("SELECT 1");
    res.json({ ok: true, service: "sdv-operational-data-api" });
  } catch (err) {
    res.status(503).json({ ok: false, error: err.message });
  }
});

app.get("/v1/health", async (_req, res) => {
  try {
    await pool.query("SELECT 1");
    res.json({ ok: true, service: "sdv-operational-data-api", version: "v1" });
  } catch (err) {
    res.status(503).json({ ok: false, error: err.message });
  }
});

/** Ingest one operational event (DataCollector OperationalDbExporter). */
app.post("/v1/ingest", async (req, res) => {
  const norm = normalizeIngestBody(req.body);
  if (norm.error) {
    res.status(400).json({ ok: false, error: norm.error });
    return;
  }

  try {
    const result = await insertOperationalEvent(pool, norm, null);

    res.status(201).json({
      ok: true,
      id: String(result.rows[0].id),
      received_at: result.rows[0].received_at,
    });
  } catch (err) {
    console.error("[ingest]", err);
    res.status(500).json({ ok: false, error: err.message });
  }
});

/**
 * Batch ingest: body { "events": [ {...}, ... ] } or a top-level JSON array.
 * DataCollector OperationalDbExporter posts here when batch size > 1.
 */
app.post("/v1/ingest/batch", async (req, res) => {
  const events = Array.isArray(req.body) ? req.body : req.body?.events;
  if (!Array.isArray(events)) {
    res.status(400).json({ ok: false, error: "events array required" });
    return;
  }
  if (events.length === 0) {
    res.status(400).json({ ok: false, error: "events must not be empty" });
    return;
  }
  if (events.length > MAX_BATCH_SIZE) {
    res.status(400).json({
      ok: false,
      error: `max ${MAX_BATCH_SIZE} events per batch`,
    });
    return;
  }

  const ingestBatchId = randomUUID();
  const client = await pool.connect();

  try {
    await client.query("BEGIN");

    const ids = [];
    for (let i = 0; i < events.length; i += 1) {
      const norm = normalizeIngestBody(events[i]);
      if (norm.error) {
        await client.query("ROLLBACK");
        console.error("[ingest/batch] validation failed at index", i, norm.error, events[i]);
        res.status(400).json({
          ok: false,
          error: norm.error,
          index: i,
        });
        return;
      }
      const result = await insertOperationalEvent(client, norm, ingestBatchId);
      ids.push(String(result.rows[0].id));
    }

    await client.query("COMMIT");

    res.status(201).json({
      ok: true,
      ingest_batch_id: ingestBatchId,
      inserted: ids.length,
      ids,
    });
  } catch (err) {
    await client.query("ROLLBACK");
    console.error("[ingest/batch]", err);
    res.status(500).json({ ok: false, error: err.message });
  } finally {
    client.release();
  }
});

/** Query stored events (dashboard / debug). */
app.get("/v1/events", async (req, res) => {
  const vehicleId = String(req.query.vehicle_id || "").trim();
  const eventType = String(req.query.event_type || "").trim();
  const view = String(req.query.view || "").trim().toLowerCase();
  const limit = Math.min(Math.max(Number(req.query.limit) || 50, 1), 500);

  let table = "operational_events";
  if (view === "shadow") {
    table = "v_shadow_mode_events";
  } else if (view === "kill_switch" || view === "kill") {
    table = "v_kill_switch_events";
  } else if (view === "flag") {
    table = "v_feature_flag_events";
  }

  const clauses = [];
  const params = [];
  if (vehicleId) {
    params.push(vehicleId);
    clauses.push(`vehicle_id = $${params.length}`);
  }
  if (eventType) {
    params.push(eventType);
    clauses.push(`event_type = $${params.length}`);
  }
  params.push(limit);

  const where = clauses.length ? `WHERE ${clauses.join(" AND ")}` : "";
  const sql = `SELECT id, vehicle_id, feature, event_type, level, role, occurred_at, payload, raw_json, received_at, ingest_batch_id
               FROM ${table}
               ${where}
               ORDER BY COALESCE(occurred_at, received_at) DESC
               LIMIT $${params.length}`;

  try {
    const result = await pool.query(sql, params);
    res.json({ ok: true, count: result.rows.length, events: result.rows });
  } catch (err) {
    console.error("[events]", err);
    res.status(500).json({ ok: false, error: err.message });
  }
});

async function waitForDb(maxAttempts = 30) {
  for (let i = 1; i <= maxAttempts; i += 1) {
    try {
      await pool.query("SELECT 1");
      return;
    } catch (err) {
      if (i === maxAttempts) {
        throw err;
      }
      await new Promise((r) => setTimeout(r, 1000));
    }
  }
}

async function main() {
  await waitForDb();
  app.listen(PORT, "0.0.0.0", () => {
    console.log(`[sdv-operational-data-api] listening on :${PORT}`);
  });
}

main().catch((err) => {
  console.error("[sdv-operational-data-api] fatal:", err);
  process.exit(1);
});
