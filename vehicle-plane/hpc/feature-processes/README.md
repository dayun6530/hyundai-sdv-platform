# Feature Processes

## Component Purpose
ADAS, Navigation, Energy 등 차량 기능 프로세스가 위치하는 논리 컴포넌트입니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/src/Feature/`

## Current Status
implemented

## Do Not Move Yet
Feature CMake target과 unit test가 현재 경로에 의존하므로 즉시 이동하지 않습니다.

## Future Refactoring Plan
도메인별 adapter 문서 작성 후 `adas/`, `navigation/`, `energy/` 단위로 단계 이동을 검토합니다.
