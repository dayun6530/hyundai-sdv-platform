# Rollout Policy Manager

## Component Purpose
Canary, group/region, percentage, Shadow/Primary 전략 등 배포 정책을 결정하는 컴포넌트입니다.

## Current Implementation Mapping
- group/region/canary 선택 UI 일부`n- `sdv-ota-manager/platform/DeploymentManager`의 fleet deploy logic 일부

## Current Status
partially implemented

## Do Not Move Yet
정책 로직이 UI 선택값과 DeploymentMgr 내부 targeting에 흩어져 있습니다.

## Future Refactoring Plan
Canary/percentage/health gate 정책 모델을 추가하고 기존 CLI 인자로 변환하는 adapter를 둡니다.
