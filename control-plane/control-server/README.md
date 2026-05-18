# Control Server

## Component Purpose
Dashboard 요청을 받아 deploy 파일, DeploymentMgr CLI, Docker runtime script, Unleash proxy와 연결하는 Control Plane API 서버입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/status-server/`n- 특히 `sdv-feature-flag-platform/status-server/server.js`

## Current Status
implemented

## Do Not Move Yet
`server.js`는 deploy 파일 read/write, CLI 실행, WSL path, runtime script 호출이 결합된 고위험 파일입니다.

## Future Refactoring Plan
Route별 service layer를 먼저 추출하고 이후 control-server 구조로 이동합니다.
