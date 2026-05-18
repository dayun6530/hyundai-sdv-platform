# External Gateway

## Component Purpose
외부 시스템, Unleash, MQTT/HTTP gateway와 Control Plane 사이의 진입점을 표현하는 계획 컴포넌트입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/status-server/server.js`의 Unleash proxy 일부`n- `Signal_I_F/` REST API 일부

## Current Status
partially implemented

## Do Not Move Yet
현재 gateway 책임이 status-server와 Signal_I_F에 섞여 있어 즉시 분리하면 API 호환성이 깨집니다.

## Future Refactoring Plan
외부 API contract 문서화 후 gateway adapter를 추가합니다.
