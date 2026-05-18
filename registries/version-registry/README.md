# Version Registry

## Component Purpose
Feature별 Primary/Candidate Version, package metadata, compatibility 정보를 제공하는 논리 registry입니다.

## Current Implementation Mapping
- `sdv-ota-manager/platform/deploy/version_map.json`
- `sdv-ota-manager/platform/deploy/package_registry.json`
- `sdv-ota-manager/platform/deploy/packages/*/*/manifest.json`
- `sdv-ota-manager/platform/DeploymentManager/src/VersionManager.cpp`
- `sdv-ota-manager/platform/DeploymentManager/include/DeploymentManager/VersionManager.h`
- `sdv-ota-manager/scripts/update_version_map.py`

## Current Status
partially implemented

## Do Not Move Yet
이 폴더는 실제 `version_map.json` 또는 `package_registry.json`을 아직 대체하지 않습니다. 두 파일은 `status-server`, dashboard, `DeploymentMgr`, self-test가 직접 참조하므로 Phase 2에서 이동하거나 schema를 변경하면 안 됩니다.

## Future Refactoring Plan
Phase 2에서는 `VersionRegistryAdapter` 설계만 도입합니다. Adapter는 기존 legacy `A`/`B` slot을 내부 호환 필드로 유지하면서 외부에는 다음 모델을 제공합니다.

- Primary Version: 현재 안정/운영 버전
- Candidate Version: 새 후보 버전
- Package metadata: Docker image, digest, checksum, compatibility

Phase 3 이후에는 read-only adapter를 먼저 구현하고, 모든 데모가 통과한 뒤 실제 Version Registry 서비스 또는 파일 구조 분리를 검토합니다.
