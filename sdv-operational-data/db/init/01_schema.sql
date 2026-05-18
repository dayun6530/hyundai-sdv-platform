-- SDV operational data store (PostgreSQL)
-- Ingested from DataCollector -> OperationalDbExporter -> operational-data-api

CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- Unified telemetry / operational events (Shadow, Kill Switch, domain signals, etc.)
CREATE TABLE IF NOT EXISTS operational_events (
    id              BIGSERIAL PRIMARY KEY,
    vehicle_id      VARCHAR(32)  NOT NULL,
    feature         VARCHAR(128),
    event_type      VARCHAR(128) NOT NULL DEFAULT 'telemetry',
    level           VARCHAR(16),
    role            VARCHAR(16),
    occurred_at     TIMESTAMPTZ,
    payload         JSONB,
    raw_json        JSONB,
    received_at     TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_operational_events_vehicle_time
    ON operational_events (vehicle_id, occurred_at DESC NULLS LAST);

CREATE INDEX IF NOT EXISTS idx_operational_events_event_type
    ON operational_events (event_type);

CREATE INDEX IF NOT EXISTS idx_operational_events_received_at
    ON operational_events (received_at DESC);

CREATE INDEX IF NOT EXISTS idx_operational_events_payload_gin
    ON operational_events USING GIN (payload jsonb_path_ops);

-- Shadow Mode: observation lane (role SHADOW or event_type contains shadow)
CREATE OR REPLACE VIEW v_shadow_mode_events AS
SELECT *
FROM operational_events
WHERE role = 'SHADOW'
   OR event_type ILIKE '%shadow%'
   OR (payload->>'event_type') ILIKE '%shadow%';

-- Kill Switch and emergency stop related events
CREATE OR REPLACE VIEW v_kill_switch_events AS
SELECT *
FROM operational_events
WHERE event_type ILIKE '%kill%'
   OR (payload->>'event_type') ILIKE '%kill%'
   OR (raw_json->>'event_type') ILIKE '%kill%';

-- Feature Flag on/off style events (optional filter for dashboards)
CREATE OR REPLACE VIEW v_feature_flag_events AS
SELECT *
FROM operational_events
WHERE event_type ILIKE '%flag%'
   OR event_type ILIKE 'feature_%';

COMMENT ON TABLE operational_events IS
    'Vehicle operational telemetry and control-plane events (ingest API v1).';
COMMENT ON VIEW v_shadow_mode_events IS 'Shadow Mode / candidate lane observations.';
COMMENT ON VIEW v_kill_switch_events IS 'Kill Switch and related safety-stop events.';
