# Docker Image Builder

## Component Purpose
Feature binary를 Docker image로 빌드하고 image digest를 manifest에 기록하는 컴포넌트입니다.

## Current Implementation Mapping
- `sdv-ota-manager/scripts/build_package_image.sh`

## Current Status
partially implemented

## Do Not Move Yet
스크립트가 CMake build output과 package directory를 직접 참조합니다.

## Future Refactoring Plan
CI에서 동일 스크립트를 호출하는 wrapper를 만든 뒤 registry publisher와 분리합니다.
