#!/usr/bin/env bash
# One-shot prep for ADAS v6.0 + car_3 demo (WSL). Run from sdv-ota-manager repo root.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OTA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FFP_ROOT="$(cd "$OTA_ROOT/../sdv-feature-flag-platform" && pwd)"
DM="$FFP_ROOT/build_unleash/DeploymentMgr"
CAR_ID="${DEMO_CAR_ID:-car_3}"
VERSION="6.0"
V_TAG="v6.0"
CAMPAIGN="campaign_adas_v6.0_car3_canary"

echo "== [0] paths =="
echo "OTA_ROOT=$OTA_ROOT"
echo "FFP_ROOT=$FFP_ROOT"
echo "CAR_ID=$CAR_ID VERSION=$V_TAG"

cd "$OTA_ROOT"

echo "== [1] demo source Adas_v6.0.cpp (from v0.5 template) =="
if [[ ! -f demo_src/Adas_v0.5.cpp ]]; then
  echo "ERROR: missing demo_src/Adas_v0.5.cpp" >&2
  exit 1
fi
cp -f demo_src/Adas_v0.5.cpp demo_src/Adas_v6.0.cpp
sed -i 's/v0\.5/v6.0/g; s/0\.5/6.0/g' demo_src/Adas_v6.0.cpp
grep -q '\[ADAS v6.0\]' demo_src/Adas_v6.0.cpp

echo "== [2] package skeleton =="
./scripts/new_package.sh --domain ADAS --feature AdasFeature --version "$VERSION"

echo "== [3] build -> build_artifacts/adas/v6.0 (required before register) =="
./scripts/build_standalone_demo.sh --domain ADAS --version "$VERSION"
test -f "build_artifacts/adas/v6.0/AdasFeature"
test -f "build_artifacts/adas/v6.0/Dockerfile"

echo "== [4] register -> deploy/packages/ADAS/v6.0 =="
./scripts/register_build_artifact.sh --domain ADAS --version "$VERSION"
test -f "platform/deploy/packages/ADAS/v6.0/AdasFeature"

echo "== [5] manifest docker_image + version_map slot B =="
python3 <<PY
import json
from pathlib import Path

manifest_p = Path("platform/deploy/packages/ADAS/v6.0/manifest.json")
with manifest_p.open(encoding="utf-8") as f:
    m = json.load(f)
m["docker_image"] = "localhost:5000/sdv/adas-feature:v6.0"
m["version"] = "6.0"
with manifest_p.open("w", encoding="utf-8") as f:
    json.dump(m, f, indent=2)
    f.write("\n")

vm_p = Path("platform/deploy/version_map.json")
with vm_p.open(encoding="utf-8") as f:
    d = json.load(f)
d["features"]["ADAS"]["B"] = {
    "version": "v6.0",
    "role": "candidate",
    "binary": "AdasFeature",
    "package_id": "ADAS_v6.0",
    "package_path": "deploy/packages/ADAS/v6.0",
}
with vm_p.open("w", encoding="utf-8") as f:
    json.dump(d, f, indent=2)
    f.write("\n")
print("OK manifest + version_map ADAS B=v6.0")
PY

echo "== [6] local registry =="
docker start local-registry 2>/dev/null || \
  docker run -d -p 5000:5000 --restart unless-stopped --name local-registry registry:2

echo "== [7] push image =="
docker tag "sdv/adas-feature:$V_TAG" "localhost:5000/sdv/adas-feature:$V_TAG"
docker push "localhost:5000/sdv/adas-feature:$V_TAG"

echo "== [8] reset $CAR_ID state + vehicle_runtime =="
docker rm -f "${CAR_ID}_agent_adas" "${CAR_ID}_agent_adas_shadow" 2>/dev/null || true
python3 <<PY
import json
from pathlib import Path

car = "$CAR_ID"
state_p = Path("platform/deploy/state") / f"{car}.json"
with state_p.open(encoding="utf-8") as f:
    s = json.load(f)
s["features"]["ADAS"].update({
    "active_slot": "A",
    "active_version": "v1.0",
    "primary_slot": "A",
    "primary_version": "v1.0",
    "primary_image": "sdv/adas-feature:v1.0",
    "shadow_enabled": False,
    "shadow_slot": "",
    "shadow_version": "",
    "shadow_mode": "inactive",
    "flag": "ON",
    "kill_switch": False,
    "kill_reason": "",
})
s["ota"].update({
    "update_mode": "READY",
    "download_status": "downloaded",
    "install_status": "not_started",
    "driver_approved": True,
})
with state_p.open("w", encoding="utf-8") as f:
    json.dump(s, f, indent=2)
    f.write("\n")

rt_p = Path("deploy/vehicle_runtime") / f"{car}.json"
with rt_p.open(encoding="utf-8") as f:
    r = json.load(f)
r["domains"]["adas"] = {
    "primary_version": "v1.0",
    "shadow_version": "",
    "binary": "AdasFeature",
    "primary_container": f"{car}_agent_adas",
    "shadow_container": f"{car}_agent_adas_shadow",
    "last_action": "reset",
    "last_message_ko": f"{car} ADAS demo reset before v6.0",
    "shadow_enabled": False,
    "shadow_mode": "inactive",
}
with rt_p.open("w", encoding="utf-8") as f:
    json.dump(r, f, indent=2)
    f.write("\n")
print("OK state + vehicle_runtime for", car)
PY

echo "== [9] campaign-validate (must fail=0) =="
if [[ ! -x "$DM" ]]; then
  echo "ERROR: DeploymentMgr not found: $DM" >&2
  echo "  Build: cd $FFP_ROOT && cmake -B build_unleash && cmake --build build_unleash -j4" >&2
  exit 1
fi
cd "$OTA_ROOT/platform"
"$DM" campaign-validate deploy/campaigns/${CAMPAIGN}.json | tee /tmp/campaign_validate_v6_car3.log
grep -q 'fail=0' /tmp/campaign_validate_v6_car3.log

echo ""
echo "=============================================="
echo " PREP OK: ADAS v6.0 ready for car_3 demo"
echo " Campaign: $CAMPAIGN"
echo " Next (Windows): run_sdv.bat -> http://127.0.0.1:5173"
echo "   Validate -> Prepare Demo READY -> Run Candidate -> Promote"
echo " CMD: tail -f logs/car_3/version_events.log (after run_sdv)"
echo "=============================================="
