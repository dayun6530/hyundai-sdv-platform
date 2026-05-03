#!/usr/bin/env bash
# demo_src/<Prefix>_v<x.y>.cpp → 호스트에서 g++ 로 바이너리 생성 → 동일 디렉터리에서 Docker 이미지 빌드(COPY 바이너리만).
set -euo pipefail

usage() {
  echo "Usage: $0 --domain <DOMAIN> --version <x.y> [--cpp <file.cpp>]" >&2
  echo "  Example: $0 --domain ADAS --version 2.0   # uses demo_src/Adas_v2.0.cpp when present" >&2
  echo "  Example: $0 --domain ADAS --version 2.0 --cpp Adas_v2.0.cpp" >&2
  echo "  Default source: demo_src/<Adas|Navigation|EvEnergy>_v<version>.cpp (no \"Feature\" in filename)." >&2
  echo "  --cpp: basename only under demo_src/." >&2
  echo "  산출물: build_artifacts/<domain>/v<ver>/{<FeatureBase>,checksum.sha256,Dockerfile} + 이미지 sdv/<domain>-feature:v<ver>" >&2
  exit 1
}

DOMAIN=""
VERSION=""
CPP_BASENAME=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --domain) DOMAIN="${2:-}"; shift 2 ;;
    --version) VERSION="${2:-}"; shift 2 ;;
    --cpp) CPP_BASENAME="${2:-}"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; usage ;;
  esac
done

if [[ -z "$DOMAIN" || -z "$VERSION" ]]; then
  usage
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OTA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEMO_SRC="$OTA_ROOT/demo_src"
mkdir -p "$DEMO_SRC"

DOMAIN_LOWER="$(echo "$DOMAIN" | tr '[:upper:]' '[:lower:]')"
case "$DOMAIN_LOWER" in
  adas) FEATURE_BASE="AdasFeature"; VERSION_PREFIX="Adas" ;;
  navigation) FEATURE_BASE="NavigationFeature"; VERSION_PREFIX="Navigation" ;;
  evenergy) FEATURE_BASE="EvEnergyFeature"; VERSION_PREFIX="EvEnergy" ;;
  *)
    echo "지원 도메인: ADAS, Navigation, EvEnergy (스크립트의 case 에 소스 매핑 추가 가능)." >&2
    exit 1
    ;;
esac

VERSION_STR="$VERSION"
VERSION_STR="${VERSION_STR#v}"
VERSION_STR="${VERSION_STR#V}"

if [[ -n "$CPP_BASENAME" ]]; then
  if [[ "$CPP_BASENAME" == *"/"* || "$CPP_BASENAME" == *".."* ]]; then
    echo "--cpp 는 demo_src 아래 파일 이름만 허용합니다 (예: Adas_v2.0.cpp)." >&2
    exit 1
  fi
  SRC_FILE="$DEMO_SRC/$CPP_BASENAME"
else
  SRC_FILE="$DEMO_SRC/${VERSION_PREFIX}_v${VERSION_STR}.cpp"
fi

if [[ ! -f "$SRC_FILE" ]]; then
  echo "데모 소스가 없습니다: $SRC_FILE" >&2
  echo "  기본 파일명: demo_src/${VERSION_PREFIX}_v<version>.cpp (예: Adas_v1.0.cpp) 또는 --cpp 로 지정." >&2
  exit 1
fi

if ! command -v g++ >/dev/null 2>&1; then
  echo "호스트에 g++ 가 필요합니다 (바이너리를 먼저 컴파일합니다)." >&2
  exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "docker 가 PATH에 없습니다." >&2
  exit 1
fi

ARTIFACT_DIR="$OTA_ROOT/build_artifacts/${DOMAIN_LOWER}/v${VERSION_STR}"
mkdir -p "$ARTIFACT_DIR"

BIN_PATH="$ARTIFACT_DIR/$FEATURE_BASE"
echo "== (1) 호스트 컴파일: $SRC_FILE -> $BIN_PATH =="
# 호스트 g++ 가 Ubuntu 22.04 이미지의 libstdc++/glibc 보다 새 심볼을 요구하면 컨테이너가 즉시 종료됨.
# C++ 런타임은 정적 링크, 런타임 베이스는 Ubuntu 24.04 로 맞춰 최신 WSL 툴체인과 glibc 를 가깝게 맞춤.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -static-libstdc++ -static-libgcc "$SRC_FILE" -o "$BIN_PATH"
chmod +x "$BIN_PATH"

echo "== (2) 체크섬 =="
if command -v sha256sum >/dev/null 2>&1; then
  (cd "$ARTIFACT_DIR" && sha256sum "$FEATURE_BASE" > checksum.sha256)
elif command -v shasum >/dev/null 2>&1; then
  (cd "$ARTIFACT_DIR" && shasum -a 256 "$FEATURE_BASE" | awk -v bin="$FEATURE_BASE" '{print $1 "  " bin}' > checksum.sha256)
else
  echo "sha256sum 또는 shasum -a 256 이 필요합니다." >&2
  exit 1
fi

IMAGE_TAG="sdv/${DOMAIN_LOWER}-feature:v${VERSION_STR}"

cat > "$ARTIFACT_DIR/Dockerfile" <<EOF
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \\
    && apt-get install -y --no-install-recommends ca-certificates \\
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY ${FEATURE_BASE} /app/${FEATURE_BASE}
RUN chmod +x /app/${FEATURE_BASE}
EOF

echo "== (3) Docker 이미지: $IMAGE_TAG (빌드 컨텍스트: $ARTIFACT_DIR) =="
(
  cd "$ARTIFACT_DIR"
  docker build -t "$IMAGE_TAG" .
)

echo ""
echo "완료: $IMAGE_TAG"
echo "  바이너리(호스트): $BIN_PATH"
echo "  체크섬:           $ARTIFACT_DIR/checksum.sha256"
echo "  다른 PC: 위 디렉터리 전체를 복사한 뒤 같은 Dockerfile으로 docker build -t $IMAGE_TAG ."
echo "  실행 예: docker run --rm $IMAGE_TAG /app/$FEATURE_BASE"
echo "  버전 인자: docker run --rm $IMAGE_TAG /app/$FEATURE_BASE ${VERSION_STR}"
