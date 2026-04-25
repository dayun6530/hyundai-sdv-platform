## MQTT Message Protocol (JSON) v1.0

This document defines the SDV project MQTT JSON protocol used between the Control Tower (server) and Vehicle Platform (agent).

### Versioning policy
- **Field `protocol_version` is mandatory** for v1 messages.
- **Backward-compatible changes**: adding optional fields is allowed (e.g. `payload.current_feature`).
- **Breaking changes**: must bump `protocol_version` (e.g. `2.0`) and update both sides.

### Envelope schema

```json
{
  "protocol_version": "1.0",
  "header": {
    "msg_id": "uuid-1234",
    "timestamp": "2024-04-16T15:00:00Z",
    "vehicle_id": "CAR_001"
  },
  "command": "DEPLOY_FEATURE",
  "payload": {
    "feature_name": "ADAS_GEN2",
    "version": "2.0.1",
    "action": "SHADOW_RUN",
    "binary_path": "/usr/bin/features/adas_v2",
    "current_feature": "ADAS_GEN1"
  },
  "error": {
    "code": 0,
    "message": "OK"
  }
}
```

### Required fields
- **`protocol_version`**: `"1.0"`
- **`header.msg_id`**: UUID string (correlation id)
- **`header.timestamp`**: ISO-8601 UTC timestamp string
- **`header.vehicle_id`**: vehicle identifier (e.g. `CAR_001`)
- **`command`**: command string
- **`payload.feature_name`**: target feature logical name

### Commands
- **`DEPLOY_FEATURE`**
  - **payload.version**: required
  - **payload.action**: recommended (`SHADOW_RUN`, `ACTIVATE`, ...)
  - **payload.binary_path**: optional/required depending on deployment mode
  - **payload.current_feature**: optional (used when action is `SHADOW_RUN`)

- **`KILL_SWITCH`**
  - **payload.feature_name**: required

### Error model
Field `error` is optional. When present:
- `error.code`: integer error code
- `error.message`: human-readable message

#### Error code registry (v1)
- **0**: OK
- **1000**: INVALID_JSON
- **1001**: MISSING_FIELD
- **1002**: INVALID_FIELD
- **1003**: UNSUPPORTED_VERSION
- **1004**: UNKNOWN_COMMAND
- **2001**: FEATURE_STOP_FAILED
- **2002**: DEPLOY_FAILED
- **3001**: PUBLISH_FAILED

