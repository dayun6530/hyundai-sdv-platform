# Deployment Command API

## Component Purpose
Control Plane에서 Vehicle Deployment Manager 또는 Signal I/F로 배포/rollback/shadow/kill 명령을 전달하는 API입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/status-server/server.js`의 `/api/fleet/deploy`, `/api/fleet/action`
- `Signal_I_F`의 deploy/shadow/kill control API 일부
- `sdv-ota-manager/platform/DeploymentManager/src/DeploymentManagerMain.cpp`의 `fleet-deploy`, `fleet-shadow`, `fleet-promote-shadow`, `fleet-rollback`, `fleet-kill`
- `sdv-ota-manager/scripts/ota_vehicle.sh`의 `deploy-shadow`, `promote`

## Current Status
implemented

## Do Not Move Yet
현재 Node API, `DeploymentMgr` CLI, `ota_vehicle.sh`, Spring/MQTT API가 서로 다른 경로로 같은 목적의 명령을 처리합니다. Phase 2에서 CLI/API 이름을 바꾸거나 파일을 이동하면 dashboard와 데모 실행 흐름이 깨질 수 있습니다.

## Future Refactoring Plan
Control Plane 명령은 바로 CLI를 바꾸지 않고 `DeploymentCommandAdapter`를 통해 legacy backend로 변환합니다.

예상 변환:

- `deployCandidate(vehicleId, feature, version)` -> `DeploymentMgr fleet-deploy vehicle ...`
- `runShadow(vehicleId, feature, version)` -> `DeploymentMgr fleet-shadow ...` 또는 `ota_vehicle.sh deploy-shadow`
- `promoteCandidateToPrimary(vehicleId, feature)` -> `DeploymentMgr fleet-promote-shadow ...` 또는 `ota_vehicle.sh promote`
- `rollbackToPreviousPrimary(vehicleId, feature)` -> `DeploymentMgr fleet-rollback ...`
- `activateKillSwitch(vehicleId, feature, reason)` -> `DeploymentMgr fleet-kill ...` 또는 `Signal_I_F` kill API

Phase 3에서는 adapter interface와 read-only/dry-run mapping부터 구현하고, 기존 API response shape은 유지합니다.
