-- Batch ingest support: group rows inserted via POST /v1/ingest/batch

ALTER TABLE operational_events
    ADD COLUMN IF NOT EXISTS ingest_batch_id UUID;

CREATE INDEX IF NOT EXISTS idx_operational_events_ingest_batch_id
    ON operational_events (ingest_batch_id)
    WHERE ingest_batch_id IS NOT NULL;
