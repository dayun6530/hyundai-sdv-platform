# Control Plane

## Component Purpose
운영자가 배포/모니터링/Signal I/O를 제어하는 서버와 UI 계층입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/dashboard/`n- `sdv-feature-flag-platform/status-server/`n- `Signal_I_F/` 일부 REST API

## Current Status
partially implemented

## Do Not Move Yet
UI와 API가 기존 상대 경로, Vite proxy, Node server path 계산에 의존합니다.

## Future Refactoring Plan
Dashboard와 status-server를 논리적으로 먼저 분리하고, API contract 안정화 후 물리 이동합니다.
