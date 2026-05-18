-- Reference labels for common event_type values (documentation + optional UI joins)

CREATE TABLE IF NOT EXISTS event_type_catalog (
    event_type   VARCHAR(128) PRIMARY KEY,
    category     VARCHAR(64)  NOT NULL,
    description  TEXT         NOT NULL
);

INSERT INTO event_type_catalog (event_type, category, description) VALUES
    ('telemetry',           'general',     'Default periodic / domain telemetry'),
    ('shadow_observation',  'shadow',      'Shadow lane mirror or validation sample'),
    ('shadow_mode_on',      'shadow',      'Shadow Mode enabled for a Primary feature'),
    ('shadow_mode_off',     'shadow',      'Shadow Mode disabled'),
    ('kill_switch',         'kill_switch', 'Kill Switch activated for a feature'),
    ('kill_switch_ack',     'kill_switch', 'Feature acknowledged Kill Switch stop'),
    ('feature_flag_on',     'flag',        'Feature flag enabled'),
    ('feature_flag_off',    'flag',        'Feature flag disabled'),
    ('deploy_promote',      'ota',         'Docker/OTA promote to Primary'),
    ('deploy_shadow',       'ota',         'Docker/OTA deploy to Shadow lane')
ON CONFLICT (event_type) DO NOTHING;
