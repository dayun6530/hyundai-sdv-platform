# Dynamic Test

## Component Purpose
런타임 demo, OTA flow, Docker Primary/Shadow 검증을 담당하는 동적 테스트 컴포넌트입니다.

## Current Implementation Mapping
- `DeploymentMgr fleet-selftest`n- `sdv-ota-manager/scripts/ota_vehicle.sh`n- demo scripts

## Current Status
partially implemented

## Do Not Move Yet
동적 테스트는 deploy/state와 Docker 환경에 의존하므로 이동/변경 시 데모가 깨질 수 있습니다.

## Future Refactoring Plan
test fixture와 runtime dependency를 분리한 뒤 CI optional job으로 확장합니다.
