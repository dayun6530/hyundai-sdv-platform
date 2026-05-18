# Data Monitoring Service

## Component Purpose
Dashboard가 차량 상태, Fleet history, 배포 결과, runtime 상태를 조회할 수 있도록 제공하는 Control Plane 조회 서비스입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/status-server/server.js`의 `/api/fleet/state`, `/api/fleet/fleet-history`n- `sdv-feature-flag-platform/dashboard/src/fleet/*`

## Current Status
implemented

## Do Not Move Yet
dashboard가 현재 response shape에 의존하므로 즉시 API 변경은 위험합니다.

## Future Refactoring Plan
조회 API contract를 고정하고 Monitoring Plane analyzer 결과를 나중에 병합합니다.
