# ADAS / AdasFeature v1.0

- **도메인:** ADAS
- **바이너리:** `AdasFeature` (`../sdv-feature-flag-platform` CMake 빌드 산출물을 `build_package_image.sh`가 복사)
- **버전:** 1.0
- **Docker 이미지:** `sdv/adas-feature:v1.0`

`scripts/build_package_image.sh`로 플랫폼 전체를 `cmake -B build_unleash -DSDV_ENABLE_UNLEASH=ON` 후 복사 → 체크섬 → Docker 빌드 → `deploy/version_map.json` 갱신을 수행합니다.
