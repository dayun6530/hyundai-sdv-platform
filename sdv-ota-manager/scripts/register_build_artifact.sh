#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 --domain <DOMAIN> --version <x.y|vX.Y> [--feature <ExecutableName>] [--source <path>]" >&2
  echo "  Registers an existing build_artifacts output into the official package store:" >&2
  echo "    build_artifacts/<domain>/v<version>/  ->  platform/deploy/packages/<DOMAIN>/v<version>/" >&2
  echo "" >&2
  echo "Notes:" >&2
  echo "  - This script does NOT build docker images. It copies build inputs (Dockerfile/binary/checksum)." >&2
  echo "  - It creates/updates manifest.json with required Docker fields (manifest.schema.json)." >&2
  exit 1
}

DOMAIN=""
VERSION=""
FEATURE=""
SOURCE_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --domain) DOMAIN="${2:-}"; shift 2 ;;
    --version) VERSION="${2:-}"; shift 2 ;;
    --feature) FEATURE="${2:-}"; shift 2 ;;
    --source) SOURCE_DIR="${2:-}"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; usage ;;
  esac
done

if [[ -z "$DOMAIN" || -z "$VERSION" ]]; then
  usage
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OTA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

normalize_domain_key() {
  # case-insensitive + strip separators (space/_/-) to make mapping stable
  echo "$1" | tr '[:upper:]' '[:lower:]' | tr -d ' _-'
}

docker_repo_for_domain() {
  # Map domain variants to canonical docker repos
  # Energy variants (Energy/EvEnergy/EVENERGY/evenergy/ev) -> sdv/evenergy-feature
  local key
  key="$(normalize_domain_key "${1:-}")"
  case "$key" in
    adas) echo "sdv/adas-feature" ;;
    navigation|nav) echo "sdv/navigation-feature" ;;
    energy|evenergy|ev) echo "sdv/evenergy-feature" ;;
    *) echo "sdv/${key:-unknown}-feature" ;;
  esac
}

# Normalize "2.0" vs "v2.0" into a single folder tag.
if [[ "${VERSION:0:1}" == "v" || "${VERSION:0:1}" == "V" ]]; then
  V_TAG="v${VERSION:1}"
  VERSION_NOV="${VERSION:1}"
else
  V_TAG="v${VERSION}"
  VERSION_NOV="${VERSION}"
fi

DOMAIN_LOWER="$(normalize_domain_key "$DOMAIN")"
SRC="${SOURCE_DIR:-$OTA_ROOT/build_artifacts/$DOMAIN_LOWER/$V_TAG}"
DEST="$OTA_ROOT/platform/deploy/packages/$DOMAIN/$V_TAG"

if [[ ! -d "$SRC" ]]; then
  echo "Source build_artifacts dir not found: $SRC" >&2
  exit 1
fi

mkdir -p "$DEST"

# Copy known build inputs (best-effort, do not assume the source is complete).
if [[ -f "$SRC/Dockerfile" ]]; then
  cp -f "$SRC/Dockerfile" "$DEST/Dockerfile"
fi
if [[ -f "$SRC/checksum.sha256" ]]; then
  cp -f "$SRC/checksum.sha256" "$DEST/checksum.sha256"
fi

# Try to detect the executable if --feature not provided.
if [[ -z "$FEATURE" ]]; then
  # Pick the first regular file that is not Dockerfile/manifest/checksum.
  detected="$(
    (cd "$SRC" && ls -1 2>/dev/null || true) \
      | grep -vE '^(Dockerfile|manifest\.json|checksum\.sha256|README\.md)$' \
      | head -n 1 || true
  )"
  FEATURE="${detected:-}"
fi

if [[ -n "$FEATURE" && -f "$SRC/$FEATURE" ]]; then
  cp -f "$SRC/$FEATURE" "$DEST/$FEATURE"
  chmod +x "$DEST/$FEATURE" || true
fi

# If the source has a manifest already, copy it first (then ensure required Docker fields exist).
if [[ -f "$SRC/manifest.json" ]]; then
  cp -f "$SRC/manifest.json" "$DEST/manifest.json"
fi

DOCKER_REPO="$(docker_repo_for_domain "$DOMAIN")"
DOCKER_IMAGE="${DOCKER_REPO}:${V_TAG}"

export DOMAIN FEATURE VERSION_NOV DOCKER_IMAGE DEST
PYBIN="python3"
command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
"$PYBIN" - <<'PY'
import json, os, time

pkg_dir = os.environ["DEST"]
domain = os.environ["DOMAIN"]
feature = os.environ.get("FEATURE") or ""
version = os.environ["VERSION_NOV"]
docker_image = os.environ["DOCKER_IMAGE"]

manifest_path = os.path.join(pkg_dir, "manifest.json")
try:
    with open(manifest_path, "r", encoding="utf-8") as f:
        doc = json.load(f)
except Exception:
    doc = {}

now = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

# Ensure manifest.schema.json required fields exist (defaults are chosen to preserve existing behavior).
doc.setdefault("domain", domain)
doc.setdefault("feature", feature or domain)  # tolerate empty feature; keep something stable
doc.setdefault("version", version)
doc.setdefault("artifact_type", "docker_image")
doc.setdefault("docker_image", docker_image)
doc.setdefault("checksum_file", "checksum.sha256")
doc.setdefault("supported_targets", ["docker"])
doc.setdefault("created_at", now)

# Recommended compatibility fields
if feature:
    doc.setdefault("executable_name", feature)
    doc.setdefault("binary_name", feature)

with open(manifest_path, "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY

echo "Registered build_artifact -> package:"
echo "  src:  $SRC"
echo "  dest: $DEST"
echo "  note: docker image is NOT built here; run docker build separately if needed."

