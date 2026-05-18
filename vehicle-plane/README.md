# Vehicle Plane

## Component Purpose
Vehicle 내부 실행 영역 전체를 표현하는 EA 논리 폴더입니다. 실제 차량/HPC에서 Feature, EventBus, Signal, Deployment Agent가 함께 동작하는 영역입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/` 전체의 차량 실행 코드`n- `sdv-ota-manager/platform/DeploymentManager/`의 차량 배포관리 코드 일부

## Current Status
partially implemented

## Do Not Move Yet
현재 실제 코드는 `sdv-feature-flag-platform`와 `sdv-ota-manager`에 분산되어 있고 CMake, scripts, dashboard/status-server가 기존 경로를 직접 참조합니다.

## Future Refactoring Plan
Phase 1에서는 논리 매핑만 유지하고, Phase 2 이후 wrapper/adapter를 만든 뒤 물리 이동을 검토합니다.
