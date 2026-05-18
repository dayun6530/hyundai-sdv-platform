# Quality Plane

## Component Purpose
Source control, build, test, static/dynamic analysis, release package 관리를 담당합니다.

## Current Implementation Mapping
- `sdv-ota-manager/scripts/`n- `sdv-feature-flag-platform/tests/`n- CMake build files

## Current Status
partially implemented

## Do Not Move Yet
빌드/패키지 스크립트가 기존 상대 경로를 직접 계산합니다.

## Future Refactoring Plan
CI/CD 도입 전 local scripts를 adapter로 유지합니다.
