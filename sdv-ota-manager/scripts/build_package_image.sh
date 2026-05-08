#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 --domain <DOMAIN> --feature <FeatureName> --version <x.y>" >&2
  echo "  Builds sdv-feature-flag-platform with CMake (-DSDV_ENABLE_UNLEASH=ON), copies the" >&2
  echo "  matching executable (CMake target name = --feature) into the package dir, then" >&2
  echo "  checksum, docker build, and update_version_map.py." >&2
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
PLATFORM_ROOT="$(cd "$OTA_ROOT/../sdv-feature-flag-platform" && pwd)"
V_TAG="v${VERSION}"
PKG_DIR="$OTA_ROOT/platform/deploy/packages/$DOMAIN/$V_TAG"
BUILD_DIR_NAME="build_unleash"
BUILD_DIR="$PLATFORM_ROOT/$BUILD_DIR_NAME"
BUILT_BINARY="$BUILD_DIR/$FEATURE"
DOMAIN_LOWER="$(echo "$DOMAIN" | tr '[:upper:]' '[:lower:]')"
IMAGE_TAG="sdv/${DOMAIN_LOWER}-feature:${V_TAG}"

if [[ ! -d "$PLATFORM_ROOT" ]]; then
  echo "플랫폼 저장소를 찾을 수 없습니다: $PLATFORM_ROOT" >&2
  exit 1
fi

if [[ ! -d "$PKG_DIR" ]]; then
  echo "패키지 디렉터리가 없습니다: $PKG_DIR (먼저 new_package.sh 실행)" >&2
  exit 1
fi

if [[ ! -f "$PKG_DIR/Dockerfile" ]]; then
  echo "Dockerfile 이 없습니다: $PKG_DIR" >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake 가 PATH에 없습니다. 설치 후 다시 실행하세요." >&2
  exit 1
fi

echo "== (1) CMake 플랫폼 빌드: $PLATFORM_ROOT ($BUILD_DIR_NAME, SDV_ENABLE_UNLEASH=ON) =="
(
  cd "$PLATFORM_ROOT"
  cmake -B "$BUILD_DIR_NAME" -DSDV_ENABLE_UNLEASH=ON
  cmake --build "$BUILD_DIR_NAME"
)

if [[ ! -f "$BUILT_BINARY" ]]; then
  echo "빌드 산출물을 찾을 수 없습니다: $BUILT_BINARY" >&2
  echo "  --feature 값이 CMake 실행 파일 타깃 이름과 동일한지 확인하세요 (예: NavigationFeature, AdasFeature)." >&2
  exit 1
fi

echo "== (2) 바이너리 패키지로 복사: $PKG_DIR/$FEATURE =="
cp -f "$BUILT_BINARY" "$PKG_DIR/$FEATURE"
chmod +x "$PKG_DIR/$FEATURE"

echo "== (3) 체크섬, Docker 이미지, version_map =="
(
  cd "$PKG_DIR"
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

# (Optional) Record docker image digest into manifest.json.
# - Preferred: Repo digest from RepoDigests (available after pull/push; may be empty for local-only builds)
# - Fallback: Image ID (.Id) which is still a sha256:... identifier, but not a registry repo digest
# This step must NOT fail the whole build when digest can't be obtained (CI/airgapped/dev envs).
echo "== (4) (optional) Docker image digest → manifest.json =="
IMG_DIGEST=""
IMG_DIGEST_SOURCE=""
if command -v docker >/dev/null 2>&1; then
  # Try repo digest first (often empty for locally-built images not pushed).
  IMG_REPO_DIGEST="$(docker image inspect "$IMAGE_TAG" --format '{{index .RepoDigests 0}}' 2>/dev/null || true)"
  if [[ -n "${IMG_REPO_DIGEST:-}" ]]; then
    # RepoDigest format: "name@sha256:...." -> keep the sha256:... part in manifest.
    IMG_DIGEST="${IMG_REPO_DIGEST#*@}"
    IMG_DIGEST_SOURCE="repo_digest"
  else
    IMG_ID="$(docker image inspect "$IMAGE_TAG" --format '{{.Id}}' 2>/dev/null || true)"
    if [[ -n "${IMG_ID:-}" ]]; then
      IMG_DIGEST="$IMG_ID"
      IMG_DIGEST_SOURCE="image_id"
    fi
  fi
else
  echo "[WARN] docker not found; skip image_digest recording." >&2
fi

if [[ -n "${IMG_DIGEST:-}" ]]; then
  echo "Recorded image digest ($IMG_DIGEST_SOURCE): $IMG_DIGEST"
  PYBIN="python3"
  command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
  "$PYBIN" - "$PKG_DIR/manifest.json" "$IMG_DIGEST" "$IMG_DIGEST_SOURCE" <<'PY'
import json, sys
path, digest, source = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    with open(path, "r", encoding="utf-8") as f:
        doc = json.load(f)
except Exception:
    doc = {}
doc["image_digest"] = digest
# Keep extra provenance as optional fields; readers may ignore.
doc["image_digest_source"] = source
with open(path, "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
else
  echo "[WARN] Could not obtain image digest for $IMAGE_TAG; leaving manifest.json image_digest unset." >&2
fi

PYBIN="python3"
command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
"$PYBIN" "$SCRIPT_DIR/update_version_map.py" --package-dir "$PKG_DIR"

echo "CMake 빌드, 바이너리 복사, 체크섬, 이미지 빌드 및 package_build_index 갱신 완료: $IMAGE_TAG"
