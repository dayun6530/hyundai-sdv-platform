# Dashboard

## Component Purpose
Fleet 상태, 패키지 등록, 버전 매핑, 배포 실행, Shadow/Kill/Rollback 제어를 제공하는 관리자 UI입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/dashboard/`

## Current Status
implemented

## Do Not Move Yet
Vite build, package.json, API client import, status-server proxy 경로가 기존 위치를 전제로 합니다.

## Future Refactoring Plan
Control Plane UI로 문서화하고 slot UI는 나중에 Primary/Candidate 표시 adapter로 감쌉니다.
