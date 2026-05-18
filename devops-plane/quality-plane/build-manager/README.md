# Build Manager

## Component Purpose
CMake, Maven, npm, Docker build를 조율하는 빌드 관리 컴포넌트입니다.

## Current Implementation Mapping
- CMake build scripts`n- `sdv-ota-manager/scripts/build_package_image.sh`

## Current Status
partially implemented

## Do Not Move Yet
현재 build script가 기존 repository layout을 직접 참조합니다.

## Future Refactoring Plan
공통 build orchestration wrapper를 추가한 뒤 내부 경로를 점진 추상화합니다.
