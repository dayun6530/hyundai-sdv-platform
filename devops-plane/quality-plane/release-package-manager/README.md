# Release & Package Manager

## Component Purpose
Feature version package 생성, manifest/checksum, Docker image build metadata, package registry 등록을 담당합니다.

## Current Implementation Mapping
- `sdv-ota-manager/scripts/new_package.sh`n- `sdv-ota-manager/scripts/build_package_image.sh`n- `sdv-ota-manager/platform/deploy/package_registry.json`n- `sdv-ota-manager/platform/deploy/packages/`

## Current Status
partially implemented

## Do Not Move Yet
package 경로는 DeploymentMgr metadata verification과 status-server package API가 직접 사용합니다.

## Future Refactoring Plan
기존 package layout을 유지하며 Docker publisher와 version registrar를 하위 adapter로 분리합니다.
