# Registries

## Component Purpose
Version Registry와 Docker Registry Mock을 포함하는 논리 저장소 영역입니다.

## Current Implementation Mapping
- `version_map.json`n- `package_registry.json`n- local Docker images

## Current Status
partially implemented

## Do Not Move Yet
현재 registry 역할이 deploy 폴더와 scripts에 섞여 있습니다.

## Future Refactoring Plan
기존 파일은 유지하고 registry adapter README/API부터 정의합니다.
