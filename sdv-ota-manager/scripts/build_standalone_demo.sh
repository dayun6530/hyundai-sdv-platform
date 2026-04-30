#!/usr/bin/env bash
# demo_src/<Feature>.cpp 만으로 초경량 이미지를 빌드 (호스트에 g++ 불필요, Docker 빌드 시 컴파일).
set -euo pipefail

usage() {
  echo "Usage: $0 --domain <DOMAIN> --version <x.y> [--cpp <file.cpp>]" >&2
  echo "  Example: $0 --domain ADAS --version 2.0" >&2
  echo "  Example (demo: separate v2 source file): $0 --domain ADAS --version 2.0 --cpp Adas_v2.0.cpp" >&2
  echo "  Default source: demo_src/<Feature>.cpp (e.g. AdasFeature.cpp)." >&2
  echo "  --cpp: basename only, must live under demo_src/ (copied in as <Feature>.cpp for the image)." >&2
  echo "  Image tag: sdv/<domain_lower>-feature:v<version>" >&2
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
  adas) FEATURE_BASE="AdasFeature" ;;
  navigation) FEATURE_BASE="NavigationFeature" ;;
  evenergy) FEATURE_BASE="EvEnergyFeature" ;;
  *)
    echo "지원 도메인: ADAS, Navigation, EvEnergy (스크립트의 case 에 소스 매핑 추가 가능)." >&2
    exit 1
    ;;
esac

if [[ -n "$CPP_BASENAME" ]]; then
  if [[ "$CPP_BASENAME" == *"/"* || "$CPP_BASENAME" == *".."* ]]; then
    echo "--cpp 는 demo_src 아래 파일 이름만 허용합니다 (예: Adas_v2.0.cpp)." >&2
    exit 1
  fi
  SRC_FILE="$DEMO_SRC/$CPP_BASENAME"
else
  SRC_FILE="$DEMO_SRC/${FEATURE_BASE}.cpp"
fi

if [[ ! -f "$SRC_FILE" ]]; then
  echo "데모 소스가 없습니다: $SRC_FILE" >&2
  echo "  기본: $DEMO_SRC/${FEATURE_BASE}.cpp 또는 --cpp 로 지정한 파일." >&2
  exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "docker 가 PATH에 없습니다." >&2
  exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/sdv-standalone-demo.XXXXXX")"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

cp "$SRC_FILE" "$TMP/${FEATURE_BASE}.cpp"

IMAGE_TAG="sdv/${DOMAIN_LOWER}-feature:v${VERSION}"

cat > "$TMP/Dockerfile" <<EOF
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \\
    && apt-get install -y --no-install-recommends g++ ca-certificates \\
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY ${FEATURE_BASE}.cpp .
RUN g++ -std=c++17 -O2 -Wall -Wextra -pedantic ${FEATURE_BASE}.cpp -o ${FEATURE_BASE}
EOF

echo "== Standalone demo build: $SRC_FILE -> $TMP/${FEATURE_BASE}.cpp -> $IMAGE_TAG =="
(
  cd "$TMP"
  docker build -t "$IMAGE_TAG" .
)

echo "완료: $IMAGE_TAG"
echo "  실행 예: docker run --rm $IMAGE_TAG /app/$FEATURE_BASE"
echo "  버전 인자: docker run --rm $IMAGE_TAG /app/$FEATURE_BASE 3.0"
