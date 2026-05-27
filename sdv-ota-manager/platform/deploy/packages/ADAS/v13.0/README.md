# ADAS / AdasFeature v13.0

- **도메인:** ADAS
- **바이너리:** `AdasFeature` (`../sdv-feature-flag-platform` CMake 빌드 산출물을 `build_package_image.sh`가 복사)
- **버전:** 13.0
- **Docker 이미지:** `sdv/adas-feature:v13.0`

`scripts/build_package_image.sh`로 플랫폼 전체를 `cmake -B build_unleash -DSDV_ENABLE_UNLEASH=ON` 후 복사 → 체크섬 → Docker 빌드 → `platform/deploy/package_build_index.json` 갱신을 수행합니다.

- **컨테이너 런타임:** Dockerfile 은 gRPC/protobuf 등 동적 링크를 위해 `ubuntu:24.04` + `apt-get install libgrpc++1.51t64` 등을 사용합니다.
