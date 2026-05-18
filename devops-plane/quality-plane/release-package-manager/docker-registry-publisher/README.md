# Docker Registry Publisher

## Component Purpose
빌드된 Docker image를 registry에 push하고 registry digest를 확보하는 컴포넌트입니다.

## Current Implementation Mapping
- 현재 실제 registry push는 미구현`n- `build_package_image.sh`의 local docker image build까지만 존재

## Current Status
planned

## Do Not Move Yet
registry 인증/주소/권한이 아직 정의되지 않았습니다.

## Future Refactoring Plan
Docker Registry mock과 실제 registry target을 분리하고 push job을 CI에 추가합니다.
