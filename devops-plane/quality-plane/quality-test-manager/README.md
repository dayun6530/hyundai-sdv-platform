# Quality/Test Manager

## Component Purpose
Unit test, self-test, integration demo 검증을 담당합니다.

## Current Implementation Mapping
- `sdv-feature-flag-platform/tests/`n- `DeploymentMgr fleet-selftest` 흐름

## Current Status
partially implemented

## Do Not Move Yet
테스트 CMake와 fleet-selftest가 현재 경로/상태 파일에 의존합니다.

## Future Refactoring Plan
CI에서 unit + fleet-selftest + dashboard build를 순차 실행하도록 확장합니다.
