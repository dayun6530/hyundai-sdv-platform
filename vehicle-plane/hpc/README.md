# Vehicle Plane / HPC

## Component Purpose
HPC 내부에서 Feature Process, Feature Flag Platform, Vehicle Deployment Manager가 함께 동작하는 영역입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/src/Feature/`n- `sdv-feature-flag-platform/src/Platform/`n- `sdv-ota-manager/platform/DeploymentManager/`

## Current Status
partially implemented

## Do Not Move Yet
현재 HPC 관련 코드가 여러 기존 루트에 분산되어 있어 즉시 이동하면 빌드와 데모가 깨질 수 있습니다.

## Future Refactoring Plan
기존 경로를 유지하면서 하위 컴포넌트별 README를 통해 EA 역할을 먼저 고정합니다.
