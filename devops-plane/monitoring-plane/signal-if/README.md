# Signal I/F

## Component Purpose
Kill Switch, Shadow, Deploy 제어 신호와 telemetry를 연결하는 Monitoring Plane 인터페이스입니다.

## Current Implementation Mapping
- `Signal_I_F/`

## Current Status
implemented

## Do Not Move Yet
Spring Boot API, MQTT protocol v1, status store를 바로 이동하면 Maven package와 protocol 호환성이 깨질 수 있습니다.

## Future Refactoring Plan
현재 API/schema를 유지하고 Control Plane signal-io-service가 호출하는 adapter로 연결합니다.
