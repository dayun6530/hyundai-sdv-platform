#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 --domain <DOMAIN> --feature <FeatureName> --version <x.y>" >&2
  echo "  Compiles src/<Feature>.cpp with g++, writes checksum.sha256, docker build, updates version_map." >&2
  exit 1
}

DOMAIN=""
FEATURE=""
VERSION=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --domain) DOMAIN="${2:-}"; shift 2 ;;
    --feature) FEATURE="${2:-}"; shift 2 ;;
    --version) VERSION="${2:-}"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; usage ;;
  esac
done

if [[ -z "$DOMAIN" || -z "$FEATURE" || -z "$VERSION" ]]; then
  usage
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OTA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
V_TAG="v${VERSION}"
PKG_DIR="$OTA_ROOT/deploy/packages/$DOMAIN/$V_TAG"
DOMAIN_LOWER="$(echo "$DOMAIN" | tr '[:upper:]' '[:lower:]')"
IMAGE_TAG="sdv/${DOMAIN_LOWER}-feature:${V_TAG}"
SRC_CPP="$PKG_DIR/src/${FEATURE}.cpp"

if [[ ! -d "$PKG_DIR" ]]; then
  echo "패키지 디렉터리가 없습니다: $PKG_DIR (먼저 new_package.sh 실행)" >&2
  exit 1
fi

if [[ ! -f "$SRC_CPP" ]]; then
  echo "소스 파일이 없습니다: $SRC_CPP" >&2
  exit 1
fi

if [[ ! -f "$PKG_DIR/Dockerfile" ]]; then
  echo "Dockerfile 이 없습니다: $PKG_DIR" >&2
  exit 1
fi

if ! command -v g++ >/dev/null 2>&1; then
  echo "g++ 가 PATH에 없습니다. 설치 후 다시 실행하세요." >&2
  exit 1
fi

(
  cd "$PKG_DIR"
  g++ "src/${FEATURE}.cpp" -o "$FEATURE"

  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$FEATURE" > checksum.sha256
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$FEATURE" | awk -v bin="$FEATURE" '{print $1 "  " bin}' > checksum.sha256
  else
    echo "sha256sum 또는 shasum -a 256 이 필요합니다." >&2
    exit 1
  fi

  docker build -t "$IMAGE_TAG" .
)

PYBIN="python3"
command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
"$PYBIN" "$SCRIPT_DIR/update_version_map.py" --package-dir "$PKG_DIR"

echo "로컬 컴파일, 체크섬, 이미지 빌드 및 version_map 갱신 완료: $IMAGE_TAG"
