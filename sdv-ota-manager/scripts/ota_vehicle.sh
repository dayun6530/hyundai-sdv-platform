#!/usr/bin/env bash
# 차량별 OTA 데모: Primary / Shadow 컨테이너를 분리해
#   (1) bootstrap — 둘 다 동일 버전(예: v1.0)
#   (2) deploy-shadow — Shadow 만 새 이미지(예: v2.0)로 교체
#   (3) promote — Primary 를 Shadow 와 동일 버전으로 승격(관측 후 확정 배포)
#
# 배포 검증: 대상 이미지 존재 확인 → 후보 컨테이너(_deploynext)로 기동·Running 검사 후
#            기존 컨테이너 제거 및 이름 교체. 실패 시 기존 인스턴스는 유지되거나 Primary 롤백.
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
  - 배포 전 이미지 존재 확인, 기동 후 Running 검사; 실패 시 이전 컨테이너 유지 또는 롤백.
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
if ! docker info >/dev/null 2>&1; then
  echo "[DEPLOY-ABORT] Docker 데몬에 연결할 수 없습니다. WSL에서 'docker ps'가 되는지 확인하세요 (Docker Desktop → Settings → Resources → WSL integration)." >&2
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

# 버전 문자열에서 선행 v/V 제거 후 이미지 태그에 사용 (v1.0 / 1.0 모두 허용)
image_for_version() {
  local ver="$1"
  ver="${ver#v}"
  ver="${ver#V}"
  echo "sdv/${DOMAIN_LOWER}-feature:v${ver}"
}

verify_target_image_or_exit() {
  local img="$1"
  if ! docker image inspect "$img" >/dev/null 2>&1; then
    echo "[DEPLOY-ABORT] Docker 이미지가 없습니다: $img (build_standalone_demo.sh 등으로 빌드 후 재시도)" >&2
    exit 1
  fi
}

read_domain_versions_from_state() {
  export RV_STATE_FILE="$STATE_DIR/${CAR}.json"
  export RV_DOM="$DOMAIN_LOWER"
  PYBIN="python3"
  command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
  eval "$("$PYBIN" - <<'PY'
import json, os, shlex
path = os.environ.get("RV_STATE_FILE", "")
dom = os.environ.get("RV_DOM", "")
pv = sv = ""
try:
    with open(path, "r", encoding="utf-8") as f:
        d = json.load(f)
    cur = d.get("domains", {}).get(dom, {})
    pv = str(cur.get("primary_version") or "").strip()
    sv = str(cur.get("shadow_version") or "").strip()
except (FileNotFoundError, json.JSONDecodeError, TypeError, AttributeError):
    pass

def q(x):
    return shlex.quote(x)

print(f"export OLD_PV={q(pv)}; export OLD_SV={q(sv)};")
PY
)"
}

docker_run_and_verify() {
  local name="$1" image="$2" role="$3" ver="$4"
  docker rm -f "$name" >/dev/null 2>&1 || true
  if ! docker run -d --name "$name" -e DEPLOY_ROLE="$role" "$image" "/app/${BINARY_NAME}" "${ver}"; then
    echo "[DEPLOY-VERIFY] docker run 실패: $name ($image)" >&2
    return 1
  fi
  # Docker Desktop / WSL 볼륨에서는 PID1 기동이 지연될 수 있음. 너무 짧으면 Running 전에 오판한다.
  sleep 1.0
  local st
  st="$(docker inspect -f '{{.State.Status}}' "$name" 2>/dev/null || echo unknown)"
  if [[ "$st" != "running" ]]; then
    echo "[DEPLOY-VERIFY] 컨테이너 '$name' 상태=$st (기동 직후 비정상 종료 가능)" >&2
    docker logs --tail 48 "$name" 2>/dev/null || true
    echo "[DEPLOY-VERIFY] 힌트: 호스트 g++ 로 빌드한 바이너리는 컨테이너 glibc 와 맞아야 합니다. 로그에 GLIBC_* not found 가 보이면" >&2
    local ver_build="${ver#v}"
    ver_build="${ver_build#V}"
    echo "  이미지 베이스를 Ubuntu 24.04 로 맞추고 재빌드: cd sdv-ota-manager && ./scripts/build_standalone_demo.sh --domain \"$DOMAIN\" --version \"$ver_build\"" >&2
    docker rm -f "$name" >/dev/null 2>&1 || true
    return 1
  fi
  return 0
}

rollback_primary_to_saved_version() {
  local old_ver="$1"
  if [[ -z "$old_ver" ]]; then
    echo "[DEPLOY-ROLLBACK] 이전 Primary 버전 정보가 없어 Primary 롤백을 건너뜁니다." >&2
    return 1
  fi
  local old_img
  old_img="$(image_for_version "$old_ver")"
  if ! docker image inspect "$old_img" >/dev/null 2>&1; then
    echo "[DEPLOY-ROLLBACK] 이전 이미지가 없습니다: $old_img — 수동 복구가 필요할 수 있습니다." >&2
    return 1
  fi
  echo "[DEPLOY-ROLLBACK] Primary 를 이전 버전으로 되돌립니다: v${old_ver#v} ($old_img)"
  docker rm -f "$PRIMARY_NAME" >/dev/null 2>&1 || true
  if ! docker_run_and_verify "$PRIMARY_NAME" "$old_img" "primary" "$old_ver"; then
    return 1
  fi
  return 0
}

# Align platform/deploy/state/<car>.json with deploy/vehicle_runtime/<car>.json:
#   primary_version  -> features.*.active_version
#   shadow_version   -> features.*.shadow_version
#   shadow_enabled   -> features.*.shadow_enabled (or inferred from version mismatch)
sync_platform_fleet_state_from_vehicle_runtime_json() {
  local cid="${1:-}"
  if [[ -z "$cid" || -z "${OTA_ROOT:-}" ]]; then
    return 0
  fi
  local rt="${OTA_ROOT}/deploy/vehicle_runtime/${cid}.json"
  local fl="${OTA_ROOT}/platform/deploy/state/${cid}.json"
  if [[ ! -f "$rt" || ! -f "$fl" ]]; then
    return 0
  fi
  PYBIN="python3"
  command -v "$PYBIN" >/dev/null 2>&1 || PYBIN="python"
  "$PYBIN" - "$rt" "$fl" <<'PY'
import json, sys

def vfmt(s):
    s = str(s or "").strip()
    if not s:
        return ""
    if s[0] in "vV" and len(s) > 1 and s[1].isdigit():
        return "v" + s[1:]
    return "v" + s.lstrip("vV")


def norm_token(s):
    s = str(s or "").strip().lower()
    if len(s) >= 2 and s[0] == "v" and s[1].isdigit():
        return s[1:]
    return s


rt_path, fl_path = sys.argv[1], sys.argv[2]
with open(rt_path, "r", encoding="utf-8") as f:
    rt = json.load(f)
with open(fl_path, "r", encoding="utf-8") as f:
    fl = json.load(f)

m = {"adas": "ADAS", "navigation": "Navigation", "evenergy": "Energy"}
features = fl.setdefault("features", {})
for dom, fk in m.items():
    cur = (rt.get("domains") or {}).get(dom) or {}
    if not isinstance(cur, dict):
        continue
    blk = features.get(fk)
    if not isinstance(blk, dict):
        continue
    pv_raw = str(cur.get("primary_version") or "").strip()
    sv_raw = str(cur.get("shadow_version") or "").strip()
    pv = vfmt(pv_raw) if pv_raw else ""
    if pv:
        blk["active_version"] = pv
    if sv_raw == "":
        blk["shadow_version"] = ""
    else:
        sv = vfmt(sv_raw)
        if sv:
            blk["shadow_version"] = sv
    se = cur.get("shadow_enabled")
    if isinstance(se, bool):
        blk["shadow_enabled"] = se
    else:
        blk["shadow_enabled"] = norm_token(pv_raw) != norm_token(sv_raw) and bool(sv_raw)

with open(fl_path, "w", encoding="utf-8") as f:
    json.dump(fl, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
}

update_state_json() {
  export STATE_FILE="$STATE_DIR/${CAR}.json"
  export CAR_ID="$CAR"
  export DOM_KEY="$DOMAIN_LOWER"
  export BIN_REF="$BINARY_NAME"
  export PV="${PRIMARY_VER:-}"
  export SV="${SHADOW_VER:-}"
  export LA="${LAST_ACTION:-}"
  export SHADOW_ENABLED="${SHADOW_ENABLED:-}"
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
if action == "bootstrap":
    cur["shadow_enabled"] = False
    if pv:
        cur["primary_version"] = pv
        cur["shadow_version"] = pv
    else:
        cur["shadow_version"] = ""
elif action == "deploy-shadow":
    cur["shadow_enabled"] = True
    if sv:
        cur["shadow_version"] = sv
    if pv:
        cur["primary_version"] = pv
elif action == "promote":
    cur["shadow_enabled"] = False
    if pv:
        cur["primary_version"] = pv
        cur["shadow_version"] = pv
    else:
        cur["shadow_version"] = ""
elif action == "promote-rollback":
    se = str(os.environ.get("SHADOW_ENABLED", "0")).strip().lower() in ("1", "true", "yes")
    cur["shadow_enabled"] = se
    if pv:
        cur["primary_version"] = pv
    cur["shadow_version"] = sv
else:
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

def _norm_ver_token(x):
    s = str(x or "").strip().lower()
    if len(s) >= 2 and s[0] == "v" and s[1].isdigit():
        s = s[1:]
    return s

def _shadow_mode(enabled, pv, sv):
    if not enabled:
        return "inactive"
    p = _norm_ver_token(pv)
    s = _norm_ver_token(sv)
    if not s or p == s:
        return "inactive"
    return "candidate"

cur["shadow_mode"] = _shadow_mode(bool(cur.get("shadow_enabled", False)), cur.get("primary_version"), cur.get("shadow_version"))
data["domains"][key] = cur
data["car_id"] = car
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
  sync_platform_fleet_state_from_vehicle_runtime_json "$CAR"
}

case "$CMD" in
  bootstrap)
    if [[ -z "$DOMAIN" || -z "$VERSION" ]]; then
      echo "bootstrap: --domain, --version 필요" >&2
      exit 1
    fi
    IMG="$(image_for_version "$VERSION")"
    verify_target_image_or_exit "$IMG"
    echo "== [bootstrap] $CAR  Primary+Shadow → $IMG ($VERSION) (검증 후 전환) =="
    NEXT_P="${PRIMARY_NAME}_deploynext"
    NEXT_S="${SHADOW_NAME}_deploynext"
    docker rm -f "$NEXT_P" "$NEXT_S" >/dev/null 2>&1 || true

    if ! docker_run_and_verify "$NEXT_P" "$IMG" "primary" "$VERSION"; then
      echo "[DEPLOY-ABORT] Primary 후보 기동 실패 — 기존 컨테이너는 변경하지 않았습니다." >&2
      exit 1
    fi
    if ! docker_run_and_verify "$NEXT_S" "$IMG" "shadow" "$VERSION"; then
      echo "[DEPLOY-ABORT] Shadow 후보 기동 실패 — 후보만 제거합니다." >&2
      docker rm -f "$NEXT_P" "$NEXT_S" >/dev/null 2>&1 || true
      exit 1
    fi

    docker rm -f "$PRIMARY_NAME" "$SHADOW_NAME" >/dev/null 2>&1 || true
    docker rename "$NEXT_P" "$PRIMARY_NAME"
    docker rename "$NEXT_S" "$SHADOW_NAME"
    sleep 0.2
    if [[ "$(docker inspect -f '{{.State.Status}}' "$PRIMARY_NAME" 2>/dev/null)" != "running" ]] ||
       [[ "$(docker inspect -f '{{.State.Status}}' "$SHADOW_NAME" 2>/dev/null)" != "running" ]]; then
      echo "[DEPLOY-ABORT] 이름 교체 후 상태 비정상 — 수동으로 docker 상태를 확인하세요." >&2
      exit 1
    fi

    LAST_MSG_KO="${BINARY_NAME}: Primary·Shadow 모두 실행 버전 v${VERSION#v}으로 기동했습니다 (bootstrap, 검증 후 전환)."
    PRIMARY_VER="$VERSION" LAST_ACTION="bootstrap" update_state_json
    echo "[OTA-DEMO] ${LAST_MSG_KO}"
    echo "[VERSION] ${CAR} ${DOMAIN}: Bootstrap completed — Primary and Shadow now run image ${IMG} (v${VERSION#v})."
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
    verify_target_image_or_exit "$IMG"
    echo "== [deploy-shadow] $CAR  Shadow 만 → $IMG ($VERSION) (검증 후 전환) =="
    if ! docker ps -a --format '{{.Names}}' | grep -qx "$PRIMARY_NAME"; then
      echo "Primary 컨테이너가 없습니다. 먼저 bootstrap 하세요: $PRIMARY_NAME" >&2
      exit 1
    fi
    NEXT_S="${SHADOW_NAME}_deploynext"
    docker rm -f "$NEXT_S" >/dev/null 2>&1 || true
    if ! docker_run_and_verify "$NEXT_S" "$IMG" "shadow" "$VERSION"; then
      echo "[DEPLOY-ABORT] Shadow 후보 기동 실패 — 기존 Shadow 는 그대로입니다." >&2
      exit 1
    fi
    docker rm -f "$SHADOW_NAME" >/dev/null 2>&1 || true
    docker rename "$NEXT_S" "$SHADOW_NAME"
    sleep 0.2
    if [[ "$(docker inspect -f '{{.State.Status}}' "$SHADOW_NAME" 2>/dev/null)" != "running" ]]; then
      echo "[DEPLOY-ABORT] Shadow 이름 교체 후 비정상" >&2
      exit 1
    fi
    LAST_MSG_KO="${BINARY_NAME}: Shadow(관측) 실행 버전이 v${VERSION#v}으로 변경되었습니다. Primary(운영)는 그대로입니다."
    SHADOW_VER="$VERSION" LAST_ACTION="deploy-shadow" update_state_json
    echo "[OTA-DEMO] ${LAST_MSG_KO}"
    echo "[VERSION] ${CAR} ${DOMAIN}: Deploy-shadow completed — Shadow updated to v${VERSION#v} (${IMG}); Primary unchanged."
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
    read_domain_versions_from_state
    IMG="$(image_for_version "$VERSION")"
    verify_target_image_or_exit "$IMG"
    echo "== [promote] $CAR  Primary → $IMG ($VERSION) (검증 후 전환) =="
    NEXT_P="${PRIMARY_NAME}_deploynext"
    docker rm -f "$NEXT_P" >/dev/null 2>&1 || true
    if ! docker_run_and_verify "$NEXT_P" "$IMG" "primary" "$VERSION"; then
      echo "[DEPLOY-ABORT] Primary 후보 기동 실패 — 기존 Primary 유지." >&2
      exit 1
    fi
    docker rm -f "$PRIMARY_NAME" >/dev/null 2>&1 || true
    docker rename "$NEXT_P" "$PRIMARY_NAME"
    sleep 0.2
    if [[ "$(docker inspect -f '{{.State.Status}}' "$PRIMARY_NAME" 2>/dev/null)" != "running" ]]; then
      echo "[DEPLOY-ABORT] Primary 교체 후 비정상" >&2
      exit 1
    fi

    if [[ "$SYNC_SHADOW" == "1" ]]; then
      echo "(옵션) Shadow 도 동일 이미지로 재기동 (검증 후 전환)"
      NEXT_SS="${SHADOW_NAME}_deploynext"
      docker rm -f "$NEXT_SS" >/dev/null 2>&1 || true
      if ! docker_run_and_verify "$NEXT_SS" "$IMG" "shadow" "$VERSION"; then
        echo "[DEPLOY-ROLLBACK] Shadow 후보 실패 — Primary 를 이전 버전으로 되돌립니다 (가능한 경우)." >&2
        if rollback_primary_to_saved_version "$OLD_PV"; then
          LAST_MSG_KO="${BINARY_NAME}: Shadow 재기동 실패로 Primary 를 이전 버전(v${OLD_PV#v})으로 롤백했습니다."
          if [[ -n "$OLD_SV" && "$OLD_PV" != "$OLD_SV" ]]; then export SHADOW_ENABLED=1; else export SHADOW_ENABLED=0; fi
          PRIMARY_VER="$OLD_PV" SHADOW_VER="$OLD_SV" LAST_ACTION="promote-rollback" update_state_json
          echo "[OTA-DEMO] ${LAST_MSG_KO}"
          exit 1
        fi
        echo "[DEPLOY-ROLLBACK] Primary 롤백도 실패했습니다. docker 및 $STATE_DIR/${CAR}.json 을 수동 확인하세요." >&2
        exit 1
      fi
      docker rm -f "$SHADOW_NAME" >/dev/null 2>&1 || true
      docker rename "$NEXT_SS" "$SHADOW_NAME"
      sleep 0.2
      if [[ "$(docker inspect -f '{{.State.Status}}' "$SHADOW_NAME" 2>/dev/null)" != "running" ]]; then
        echo "[DEPLOY-ROLLBACK] Shadow 교체 후 비정상 — Primary 롤백 시도" >&2
        if rollback_primary_to_saved_version "$OLD_PV"; then
          LAST_MSG_KO="${BINARY_NAME}: Shadow 전환 후 비정상으로 Primary 를 이전 버전으로 롤백했습니다."
          if [[ -n "$OLD_SV" && "$OLD_PV" != "$OLD_SV" ]]; then export SHADOW_ENABLED=1; else export SHADOW_ENABLED=0; fi
          PRIMARY_VER="$OLD_PV" SHADOW_VER="$OLD_SV" LAST_ACTION="promote-rollback" update_state_json
        fi
        exit 1
      fi
      LAST_MSG_KO="${BINARY_NAME}: Primary(운영) 실행 버전이 v${VERSION#v}으로 승격되었고, Shadow도 동일 버전으로 맞췄습니다."
    else
      LAST_MSG_KO="${BINARY_NAME}: Primary(운영) 실행 버전이 v${VERSION#v}으로 승격되었습니다."
    fi
    PRIMARY_VER="$VERSION" LAST_ACTION="promote" update_state_json
    echo "[OTA-DEMO] ${LAST_MSG_KO}"
    if [[ "$SYNC_SHADOW" == "1" ]]; then
      echo "[VERSION] ${CAR} ${DOMAIN}: Promote completed — Primary and Shadow updated to v${VERSION#v} (${IMG})."
    else
      echo "[VERSION] ${CAR} ${DOMAIN}: Promote completed — Primary updated to v${VERSION#v} (${IMG})."
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
