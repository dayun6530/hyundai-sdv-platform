# Feature Deployment Service

## Component Purpose
배포 캠페인, rollout policy, fleet targeting, deployment command API를 묶는 Control Plane 배포 서비스입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/status-server/server.js`의 `/api/fleet/*` 일부`n- `sdv-feature-flag-platform/dashboard/src/fleet/*`

## Current Status
partially implemented

## Do Not Move Yet
현재 명확한 service boundary 없이 UI/API/CLI가 직접 결합되어 있습니다.

## Future Refactoring Plan
campaign-manager, rollout-policy-manager, fleet-targeting-manager, deployment-command-api를 adapter 단위로 분리합니다.
