#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 --domain <DOMAIN> --feature <FeatureName> --version <x.y>" >&2
  echo "  Creates platform/deploy/packages/<DOMAIN>/v<version>/ with Dockerfile and manifest.json." >&2
  echo "  Run build_package_image.sh to CMake-build the platform and copy the binary." >&2
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
PKG_DIR="$OTA_ROOT/platform/deploy/packages/$DOMAIN/$V_TAG"

mkdir -p "$PKG_DIR"

normalize_domain_key() {
  echo "$1" | tr '[:upper:]' '[:lower:]' | tr -d ' _-'
}

docker_repo_for_domain() {
  local key
  key="$(normalize_domain_key "${1:-}")"
  case "$key" in
    adas) echo "sdv/adas-feature" ;;
    navigation|nav) echo "sdv/navigation-feature" ;;
    energy|evenergy|ev) echo "sdv/evenergy-feature" ;;
    *) echo "sdv/${key:-unknown}-feature" ;;
  esac
}

DOCKER_REPO="$(docker_repo_for_domain "$DOMAIN")"
DOCKER_IMAGE="${DOCKER_REPO}:${V_TAG}"

cat > "$PKG_DIR/Dockerfile" <<'DOCKERHEAD'
# Ubuntu 22.04 (jammy) 기본 저장소에는 libgrpc++.so.1.51 이 없어서,
# WSL/최신 툴체인에서 링크된 gRPC C++ 바이너리는 noble + libgrpc++1.51t64 가 맞는 경우가 많습니다.
# (반드시 jammy 이미지가 필요하면: 멀티스테이지로 빌드 호스트의 .so 를 복사하거나, 호스트와 동일 OS로 맞추세요.)
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libc6 \
        libstdc++6 \
        libgcc-s1 \
        libatomic1 \
        zlib1g \
        libssl3t64 \
        libcurl4t64 \
        libcares2 \
        libre2-10 \
        libgrpc++1.51t64 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
DOCKERHEAD
# COPY / 바이너리 이름은 패키지마다 다름 (heredoc에서 변수 확장)
cat >> "$PKG_DIR/Dockerfile" <<EOF
COPY $FEATURE /app/$FEATURE
RUN chmod +x /app/$FEATURE
EOF

export DOMAIN FEATURE VERSION DOCKER_IMAGE PKG_DIR="$PKG_DIR"
PYBIN="python3"
command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
"$PYBIN" - <<'PY'
import json, os, time
pkg = os.environ["PKG_DIR"]
now = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
doc = {
    "domain": os.environ["DOMAIN"],
    "feature": os.environ["FEATURE"],
    "version": os.environ["VERSION"],
    # ---- packaging intent (Docker image is the long-term deployment unit) ----
    "artifact_type": "docker_image",
    # Keep the legacy key for backward compatibility with existing notes/tools.
    "slot_role": "candidate",
    "default_role": "candidate",
    "docker_image": os.environ["DOCKER_IMAGE"],
    # build_artifacts / packages store checksum for the packaged executable that is copied into the image.
    # The Docker image digest is available only after `docker build` (see build_package_image.sh / README).
    "checksum_file": "checksum.sha256",
    "executable_name": os.environ["FEATURE"],
    "binary_name": os.environ["FEATURE"],
    "created_at": now,
    "supported_targets": ["docker"],
}
with open(os.path.join(pkg, "manifest.json"), "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY

cat > "$PKG_DIR/README.md" <<EOF
# ${DOMAIN} / ${FEATURE} ${V_TAG}

- **도메인:** ${DOMAIN}
- **바이너리:** \`${FEATURE}\` (\`../sdv-feature-flag-platform\` CMake 빌드 산출물을 \`build_package_image.sh\`가 복사)
- **버전:** ${VERSION}
- **Docker 이미지:** \`${DOCKER_IMAGE}\`

\`scripts/build_package_image.sh\`로 플랫폼 전체를 \`cmake -B build_unleash -DSDV_ENABLE_UNLEASH=ON\` 후 복사 → 체크섬 → Docker 빌드 → \`platform/deploy/package_build_index.json\` 갱신을 수행합니다.

- **컨테이너 런타임:** Dockerfile 은 gRPC/protobuf 등 동적 링크를 위해 \`ubuntu:24.04\` + \`apt-get install libgrpc++1.51t64\` 등을 사용합니다.
EOF

echo "패키지(Dockerfile, manifest) 생성 완료: $PKG_DIR"
