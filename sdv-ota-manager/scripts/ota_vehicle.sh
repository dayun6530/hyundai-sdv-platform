#!/usr/bin/env bash
# 차량별 OTA 데모: Primary / Shadow 컨테이너를 분리해
#   (1) bootstrap — 둘 다 동일 버전(예: v1.0)
#   (2) deploy-shadow — Shadow 만 새 이미지(예: v2.0)로 교체
#   (3) promote — Primary 를 Shadow 와 동일 버전으로 승격(관측 후 확정 배포)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OTA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
STATE_DIR="$OTA_ROOT/deploy/vehicle_runtime"
mkdir -p "$STATE_DIR"

usage() {
  cat >&2 <<'USAGE'
Usage:
  ota_vehicle.sh bootstrap   --car <car_N> --domain <DOMAIN> --version <x.y>
  ota_vehicle.sh deploy-shadow --car <car_N> --domain <DOMAIN> --version <x.y>
  ota_vehicle.sh promote     --car <car_N> --domain <DOMAIN> --version <x.y> [--sync-shadow]
  ota_vehicle.sh status      --car <car_N> --domain <DOMAIN>
  ota_vehicle.sh logs-primary --car <car_N> --domain <DOMAIN>
  ota_vehicle.sh logs-shadow  --car <car_N> --domain <DOMAIN>

Examples:
  ota_vehicle.sh bootstrap --car car_1 --domain ADAS --version 1.0
  ota_vehicle.sh deploy-shadow --car car_1 --domain ADAS --version 2.0
  ota_vehicle.sh promote --car car_1 --domain ADAS --version 2.0

Notes:
  - 이미지 태그: sdv/<domain>-feature:v<x.y> (build_standalone_demo.sh 로 빌드)
  - 컨테이너 이름: <car>_agent_<domain> (Primary), <car>_agent_<domain>_shadow (Shadow)
  - 상태 JSON: deploy/vehicle_runtime/<car>.json
USAGE
  exit 1
}

if [[ $# -lt 1 ]]; then
  usage
fi

CMD="$1"
shift

CAR=""
DOMAIN=""
VERSION=""
SYNC_SHADOW="0"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --car) CAR="${2:-}"; shift 2 ;;
    --domain) DOMAIN="${2:-}"; shift 2 ;;
    --version) VERSION="${2:-}"; shift 2 ;;
    --sync-shadow) SYNC_SHADOW="1"; shift 1 ;;
    -h|--help) usage ;;
    *) echo "Unknown option: $1" >&2; usage ;;
  esac
done

if [[ -z "$CAR" ]]; then
  echo "--car 필수 (예: car_1)" >&2
  usage
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "docker 가 PATH에 없습니다." >&2
  exit 1
fi

DOMAIN_LOWER="$(echo "${DOMAIN:-}" | tr '[:upper:]' '[:lower:]')"
case "$DOMAIN_LOWER" in
  adas) BINARY_NAME="AdasFeature" ;;
  navigation) BINARY_NAME="NavigationFeature" ;;
  evenergy) BINARY_NAME="EvEnergyFeature" ;;
  *)
    echo "지원 도메인: ADAS, Navigation, EvEnergy" >&2
    exit 1
    ;;
esac

PRIMARY_NAME="${CAR}_agent_${DOMAIN_LOWER}"
SHADOW_NAME="${PRIMARY_NAME}_shadow"

image_for_version() {
  local ver="$1"
  echo "sdv/${DOMAIN_LOWER}-feature:v${ver}"
}

run_container() {
  local name="$1"
  local image="$2"
  local role="$3"
  local ver="$4"
  docker rm -f "$name" >/dev/null 2>&1 || true
  docker run -d --name "$name" \
    -e DEPLOY_ROLE="$role" \
    "$image" "/app/${BINARY_NAME}" "${ver}"
}

update_state_json() {
  # PYTHON_JSON_UPDATE
  export STATE_FILE="$STATE_DIR/${CAR}.json"
  export CAR_ID="$CAR"
  export DOM_KEY="$DOMAIN_LOWER"
  export BIN_REF="$BINARY_NAME"
  export PV="${PRIMARY_VER:-}"
  export SV="${SHADOW_VER:-}"
  export LA="${LAST_ACTION:-}"
  export LAST_MSG_KO="${LAST_MSG_KO:-}"
  PYBIN="python3"
  command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
  "$PYBIN" - <<'PY'
import json, os
path = os.environ["STATE_FILE"]
car = os.environ["CAR_ID"]
key = os.environ["DOM_KEY"]
binary = os.environ["BIN_REF"]
pv = os.environ.get("PV", "")
sv = os.environ.get("SV", "")
action = os.environ.get("LA", "")
try:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
except FileNotFoundError:
    data = {"car_id": car, "domains": {}}
if "domains" not in data:
    data["domains"] = {}
cur = data["domains"].get(key, {})
if pv:
    cur["primary_version"] = pv
if sv:
    cur["shadow_version"] = sv
cur["binary"] = binary
cur["primary_container"] = f"{car}_agent_{key}"
cur["shadow_container"] = f"{car}_agent_{key}_shadow"
if action:
    cur["last_action"] = action
msg_ko = os.environ.get("LAST_MSG_KO", "").strip()
if msg_ko:
    cur["last_message_ko"] = msg_ko
data["domains"][key] = cur
data["car_id"] = car
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
}

case "$CMD" in
  bootstrap)
    if [[ -z "$DOMAIN" || -z "$VERSION" ]]; then
      echo "bootstrap: --domain, --version 필요" >&2
      exit 1
    fi
    IMG="$(image_for_version "$VERSION")"
    echo "== [bootstrap] $CAR  Primary+Shadow → $IMG ($VERSION) =="
    run_container "$PRIMARY_NAME" "$IMG" "primary" "$VERSION"
    run_container "$SHADOW_NAME" "$IMG" "shadow" "$VERSION"
    LAST_MSG_KO="${BINARY_NAME}: Primary·Shadow 모두 실행 버전 v${VERSION}으로 기동했습니다 (bootstrap)."
    PRIMARY_VER="$VERSION" SHADOW_VER="$VERSION" LAST_ACTION="bootstrap" update_state_json
    echo "[OTA-DEMO] ${LAST_MSG_KO}"
    echo "[VERSION] ${CAR} ${DOMAIN}: Bootstrap completed — Primary and Shadow now run image ${IMG} (v${VERSION})."
    sleep 1
    echo "Primary: $PRIMARY_NAME"
    docker logs --tail 6 "$PRIMARY_NAME" 2>/dev/null || true
    echo "Shadow:  $SHADOW_NAME"
    docker logs --tail 6 "$SHADOW_NAME" 2>/dev/null || true
    echo "상태 파일: $STATE_DIR/${CAR}.json"
    ;;

  deploy-shadow)
    if [[ -z "$DOMAIN" || -z "$VERSION" ]]; then
      echo "deploy-shadow: --domain, --version 필요" >&2
      exit 1
    fi
    IMG="$(image_for_version "$VERSION")"
    echo "== [deploy-shadow] $CAR  Shadow 만 → $IMG ($VERSION) =="
    if ! docker ps -a --format '{{.Names}}' | grep -qx "$PRIMARY_NAME"; then
      echo "Primary 컨테이너가 없습니다. 먼저 bootstrap 하세요: $PRIMARY_NAME" >&2
      exit 1
    fi
    run_container "$SHADOW_NAME" "$IMG" "shadow" "$VERSION"
    # Primary 버전/컨테이너는 그대로 — JSON 은 shadow_version 만 갱신 (primary_version 은 기존 유지)
    LAST_MSG_KO="${BINARY_NAME}: Shadow(관측) 실행 버전이 v${VERSION}으로 변경되었습니다. Primary(운영)는 그대로입니다."
    SHADOW_VER="$VERSION" LAST_ACTION="deploy-shadow" update_state_json
    echo "[OTA-DEMO] ${LAST_MSG_KO}"
    echo "[VERSION] ${CAR} ${DOMAIN}: Deploy-shadow completed — Shadow updated to v${VERSION} (${IMG}); Primary unchanged."
    sleep 1
    echo "Shadow 로그:"
    docker logs --tail 8 "$SHADOW_NAME" 2>/dev/null || true
    echo "상태 파일: $STATE_DIR/${CAR}.json"
    ;;

  promote)
    if [[ -z "$DOMAIN" || -z "$VERSION" ]]; then
      echo "promote: --domain, --version 필요" >&2
      exit 1
    fi
    IMG="$(image_for_version "$VERSION")"
    echo "== [promote] $CAR  Primary → $IMG ($VERSION) =="
    run_container "$PRIMARY_NAME" "$IMG" "primary" "$VERSION"
    if [[ "$SYNC_SHADOW" == "1" ]]; then
      echo "(옵션) Shadow 도 동일 이미지로 재기동"
      run_container "$SHADOW_NAME" "$IMG" "shadow" "$VERSION"
      LAST_MSG_KO="${BINARY_NAME}: Primary(운영) 실행 버전이 v${VERSION}으로 승격되었고, Shadow도 동일 버전으로 맞췄습니다."
    else
      LAST_MSG_KO="${BINARY_NAME}: Primary(운영) 실행 버전이 v${VERSION}으로 승격되었습니다."
    fi
    PRIMARY_VER="$VERSION" SHADOW_VER="$VERSION" LAST_ACTION="promote" update_state_json
    echo "[OTA-DEMO] ${LAST_MSG_KO}"
    if [[ "$SYNC_SHADOW" == "1" ]]; then
      echo "[VERSION] ${CAR} ${DOMAIN}: Promote completed — Primary and Shadow updated to v${VERSION} (${IMG})."
    else
      echo "[VERSION] ${CAR} ${DOMAIN}: Promote completed — Primary updated to v${VERSION} (${IMG})."
    fi
    sleep 1
    echo "Primary 로그:"
    docker logs --tail 8 "$PRIMARY_NAME" 2>/dev/null || true
    echo "Shadow 로그:"
    docker logs --tail 4 "$SHADOW_NAME" 2>/dev/null || true
    echo "상태 파일: $STATE_DIR/${CAR}.json"
    ;;

  status)
    if [[ -z "$DOMAIN_LOWER" ]]; then
      echo "status: --domain 필요 (Primary/Shadow 이름에 도메인이 들어감)." >&2
      exit 1
    fi
    echo "== [status] $CAR ($DOMAIN_LOWER) =="
    for n in "$PRIMARY_NAME" "$SHADOW_NAME"; do
      if docker ps -a --format '{{.Names}}' | grep -qx "$n"; then
        echo "--- $n ---"
        docker inspect --format 'image={{.Config.Image}} status={{.State.Status}}' "$n" 2>/dev/null || true
      else
        echo "--- $n (없음) ---"
      fi
    done
    if [[ -f "$STATE_DIR/${CAR}.json" ]]; then
      echo "--- $STATE_DIR/${CAR}.json ---"
      cat "$STATE_DIR/${CAR}.json"
    fi
    ;;

  logs-primary)
    [[ -z "$DOMAIN" ]] && { echo "--domain 필요" >&2; exit 1; }
    docker logs "$PRIMARY_NAME"
    ;;

  logs-shadow)
    [[ -z "$DOMAIN" ]] && { echo "--domain 필요" >&2; exit 1; }
    docker logs "$SHADOW_NAME"
    ;;

  *)
    echo "알 수 없는 명령: $CMD" >&2
    usage
    ;;
esac
