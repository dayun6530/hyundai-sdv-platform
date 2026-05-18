# Version Registry Registrar

## Component Purpose
빌드/검증된 Candidate Version을 Version Registry에 등록하는 컴포넌트입니다.

## Current Implementation Mapping
- `sdv-ota-manager/scripts/update_version_map.py`n- `sdv-feature-flag-platform/status-server/server.js`의 `/api/fleet/version-map/assign`

## Current Status
partially implemented

## Do Not Move Yet
현재 legacy slot/A/B map을 직접 수정하므로 즉시 Primary/Candidate 모델로 바꾸면 위험합니다.

## Future Refactoring Plan
VersionRegistryAdapter를 통해 legacy map에 기록하고 외부 용어는 Primary/Candidate로 노출합니다.
