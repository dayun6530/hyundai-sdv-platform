# Fleet Targeting Manager

## Component Purpose
Fleet, vehicle, group, region 기준으로 배포 대상 차량을 산출하는 컴포넌트입니다.

## Current Implementation Mapping
- `sdv-ota-manager/platform/config/fleet/vehicles.json`n- `sdv-ota-manager/platform/DeploymentManager/src/VehicleRegistry.cpp`n- `sdv-feature-flag-platform/status-server/server.js`의 `/api/fleet/vehicles`

## Current Status
implemented

## Do Not Move Yet
vehicles.json과 VehicleRegistry가 기존 경로를 직접 사용하며 dashboard가 같은 데이터를 조회합니다.

## Future Refactoring Plan
targeting API를 먼저 정의하고 VehicleRegistry를 backend adapter로 유지합니다.
