# Campaign Manager

## Component Purpose
특정 Feature Candidate Version을 특정 Fleet 대상에 적용하는 캠페인을 정의/추적하는 컴포넌트입니다.

## Current Implementation Mapping
- 명확한 campaign entity는 아직 없음`n- `sdv-feature-flag-platform/dashboard/src/fleet/FleetDeployPanel.jsx` 일부`n- `sdv-feature-flag-platform/status-server/server.js`의 `/api/fleet/deploy` 일부

## Current Status
partially implemented

## Do Not Move Yet
현재 campaign은 저장 객체가 아니라 즉시 실행 form/CLI 호출에 가깝습니다.

## Future Refactoring Plan
먼저 campaign DTO와 history mapping을 정의하고 기존 fleet-deploy를 실행 backend로 둡니다.
