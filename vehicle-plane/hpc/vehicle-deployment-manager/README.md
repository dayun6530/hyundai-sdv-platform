# Vehicle Deployment Manager

## Component Purpose
차량/HPC 관점의 OTA 배포 상태 관리, Candidate 수신, Primary 관리, Shadow 연동, Rollback, Kill Switch 연동을 담당합니다.

## Current Implementation Mapping
- `sdv-ota-manager/platform/DeploymentManager/`
- `sdv-ota-manager/platform/deploy/`
- `sdv-ota-manager/scripts/ota_vehicle.sh`
- `sdv-ota-manager/platform/deploy/state/car_*.json`
- `sdv-ota-manager/platform/deploy/history/fleet_history.jsonl`
- `sdv-ota-manager/platform/deploy/rollback/`

## Current Status
implemented

## Do Not Move Yet
기존 `DeploymentManager` class와 `DeploymentMgr` CLI는 당장 rename하지 않습니다. `status-server`, dashboard, self-test, scripts가 현재 경로와 명령 형태를 직접 참조하므로 Phase 2에서 실제 이동/rename/schema 변경을 하면 데모가 깨질 수 있습니다.

## Future Refactoring Plan
Phase 2에서는 Adapter 설계를 통해 Primary/Candidate 모델을 도입합니다.

- `VersionRegistryAdapter`: legacy `version_map.json`/`package_registry.json`을 Primary/Candidate view로 노출
- `DeploymentStateAdapter`: `active_*`, `primary_*`, `pending_active_*`, `shadow_*`를 안정적인 상태 모델로 변환
- `DeploymentCommandAdapter`: Primary/Candidate 명령을 기존 `DeploymentMgr` CLI 또는 `ota_vehicle.sh` 호출로 변환

Phase 3 이후 read-only adapter부터 구현하고, 기존 `DeploymentManager` 이름은 compatibility layer로 유지합니다.
