# Docker Registry Mock

## Component Purpose
실제 Docker Registry가 도입되기 전 local Docker image와 digest metadata를 표현하는 논리 컴포넌트입니다.

## Current Implementation Mapping
- 실제 Docker Registry는 현재 미구현`n- `sdv-ota-manager/scripts/build_package_image.sh`의 local docker image build

## Current Status
partially implemented

## Do Not Move Yet
현재 Docker image는 local daemon과 manifest metadata에 의존합니다.

## Future Refactoring Plan
CI/CD에서 registry push/pull을 추가하기 전 mock registry 역할을 문서화합니다.
