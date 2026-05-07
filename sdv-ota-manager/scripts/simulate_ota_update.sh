#!/usr/bin/env bash
# OTA 업데이트 시뮬레이션: 기존 car_agent_<domain> 컨테이너 제거 후 새 버전 이미지 실행, 로그로 검증.
#
# 차량별 Primary + Shadow(배포 → Shadow 먼저 → promote) 흐름은 scripts/ota_vehicle.sh 를 사용하세요.
#   예: ./ota_vehicle.sh bootstrap --car car_1 --domain ADAS --version 1.0
#       ./ota_vehicle.sh deploy-shadow --car car_1 --domain ADAS --version 2.0
#       ./ota_vehicle.sh promote --car car_1 --domain ADAS --version 2.0
set -euo pipefail

usage() {
  echo "Usage: $0 --domain <DOMAIN> --version <x.y>" >&2
  echo "  Example: $0 --domain ADAS --version 2.0" >&2
  echo "  Stops/removes car_agent_<domain_lower>, runs sdv/<domain_lower>-feature:v<x.y>," >&2
  echo "  then prints docker logs (expects image to contain /app/<FeatureBinary>)." >&2
  exit 1
}

DOMAIN=""
VERSION=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --domain) DOMAIN="${2:-}"; shift 2 ;;
    --version) VERSION="${2:-}"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; usage ;;
  esac
done

if [[ -z "$DOMAIN" || -z "$VERSION" ]]; then
  usage
fi

DOMAIN_LOWER="$(echo "$DOMAIN" | tr '[:upper:]' '[:lower:]')"
CNAME="car_agent_${DOMAIN_LOWER}"
IMAGE_TAG="sdv/${DOMAIN_LOWER}-feature:v${VERSION}"

# 도메인 → 패키지에 포함된 CMake 실행 파일 이름 (build_package_image.sh 의 --feature 와 동일해야 함)
case "$DOMAIN_LOWER" in
  adas) BINARY_NAME="AdasFeature" ;;
  navigation) BINARY_NAME="NavigationFeature" ;;
  evenergy) BINARY_NAME="EvEnergyFeature" ;;
  *)
    echo "지원 도메인: ADAS, Navigation, EvEnergy (필요 시 스크립트의 case 에 매핑 추가)." >&2
    exit 1
    ;;
esac

if ! command -v docker >/dev/null 2>&1; then
  echo "docker 가 PATH에 없습니다." >&2
  exit 1
fi

echo "== [OTA 시뮬레이션] 컨테이너: $CNAME  이미지: $IMAGE_TAG  바이너리: $BINARY_NAME =="

echo "기존 컨테이너 강제 제거(있으면): $CNAME"
docker rm -f "$CNAME" >/dev/null 2>&1 || true

echo "새 버전 실행: docker run -d --name $CNAME $IMAGE_TAG ..."
docker run -d --name "$CNAME" "$IMAGE_TAG" "/app/${BINARY_NAME}" "${VERSION}"

echo "컨테이너 기동 대기 (2s)..."
sleep 2

echo ""
echo "---------- docker logs $CNAME ----------"
docker logs "$CNAME"
echo "---------- end of logs ----------"
