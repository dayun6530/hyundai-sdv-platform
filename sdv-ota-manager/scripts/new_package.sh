#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 --domain <DOMAIN> --feature <FeatureName> --version <x.y>" >&2
  echo "  Creates deploy/packages/<DOMAIN>/v<version>/ with src/<Feature>.cpp template (no compile yet)." >&2
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
DOMAIN_LOWER="$(echo "$DOMAIN" | tr '[:upper:]' '[:lower:]')"
V_TAG="v${VERSION}"
PKG_DIR="$OTA_ROOT/deploy/packages/$DOMAIN/$V_TAG"
SRC_DIR="$PKG_DIR/src"
SRC_FILE="$SRC_DIR/${FEATURE}.cpp"

mkdir -p "$SRC_DIR"

cat > "$SRC_FILE" <<EOF
#include <iostream>

int main() {
    std::cout << "domain: ${DOMAIN}\n";
    std::cout << "feature: ${FEATURE}\n";
    std::cout << "version: ${VERSION}\n";
    return 0;
}
EOF

DOCKER_IMAGE="sdv/${DOMAIN_LOWER}-feature:${V_TAG}"

cat > "$PKG_DIR/Dockerfile" <<EOF
FROM ubuntu:22.04
WORKDIR /app
COPY $FEATURE /app/$FEATURE
RUN chmod +x /app/$FEATURE
EOF

export DOMAIN FEATURE VERSION DOCKER_IMAGE PKG_DIR="$PKG_DIR"
PYBIN="python3"
command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
"$PYBIN" - <<'PY'
import json, os
pkg = os.environ["PKG_DIR"]
doc = {
    "domain": os.environ["DOMAIN"],
    "feature": os.environ["FEATURE"],
    "version": os.environ["VERSION"],
    "slot_role": "candidate",
    "docker_image": os.environ["DOCKER_IMAGE"],
    "checksum_file": "checksum.sha256",
    "binary_name": os.environ["FEATURE"],
}
with open(os.path.join(pkg, "manifest.json"), "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY

cat > "$PKG_DIR/README.md" <<EOF
# ${DOMAIN} / ${FEATURE} ${V_TAG}

- **도메인:** ${DOMAIN}
- **소스:** \`src/${FEATURE}.cpp\` (템플릿)
- **바이너리:** \`${FEATURE}\` (\`build_package_image.sh\`에서 로컬 g++ 빌드 후 이미지에 포함)
- **버전:** ${VERSION}
- **Docker 이미지:** \`${DOCKER_IMAGE}\`

\`scripts/build_package_image.sh\` 로 로컬 컴파일 → 체크섬 → Docker 빌드 → \`deploy/version_map.json\` 갱신을 수행합니다.
EOF

echo "패키지(소스 템플릿) 생성 완료: $PKG_DIR"
echo "  소스: $SRC_FILE"
