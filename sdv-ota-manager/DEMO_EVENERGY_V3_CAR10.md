# EvEnergy v3.0 — 이미지 빌드 & Campaign 데모 시나리오

ADAS v13.0 / car_10 canary 시나리오와 **동일한 단계·흐름**입니다.  
경로·도메인·이미지 이름만 이 저장소(`teamproject`)에 맞게 바꿨습니다.

| ADAS v13.0 (원본) | EvEnergy v3.0 (이 시나리오) |
|-------------------|---------------------------|
| `AdasFeature` | `EvEnergyFeature` |
| `--domain ADAS` (new_package) | `--domain Energy` |
| `--domain ADAS` (standalone) | `--domain EvEnergy` |
| `sdv/adas-feature` | `sdv/evenergy-feature` |
| Candidate **v13.0** | Candidate **v3.0** |
| Primary bootstrap **v4.0** (car_10 ADAS) | Primary bootstrap **v2.0** (car_10 Energy) |
| `campaign_adas_v13.0_car10_canary` | `campaign_energy_v3.0_car10_canary` |

---

## 0단계 — (Windows) 플랫폼 기동

```powershell
cd "C:\Users\82108\Desktop\teamproject\sdv-feature-flag-platform"
.\run_sdv.bat
```

브라우저:

- http://127.0.0.1:5173 (또는)
- OTA 대시보드 http://127.0.0.1:5170/?view=dashboard

---

## 1단계 — (WSL) 경로 변수

```bash
export ROOT="/mnt/c/Users/82108/Desktop/teamproject"
export OTA_ROOT="$ROOT/sdv-ota-manager"
export PLATFORM_ROOT="$OTA_ROOT/platform"
export FFP_ROOT="$ROOT/sdv-feature-flag-platform"

# DeploymentMgr: ota-manager 빌드 우선, 없으면 feature-flag-platform build_unleash
if [[ -x "$OTA_ROOT/platform/DeploymentManager/build/DeploymentMgr" ]]; then
  export DM="$OTA_ROOT/platform/DeploymentManager/build/DeploymentMgr"
else
  export DM="$FFP_ROOT/build_unleash/DeploymentMgr"
fi

cd "$OTA_ROOT"
```

`DeploymentMgr` 없으면 **한 번만**:

```bash
cd "$FFP_ROOT"
cmake -B build_unleash -DCMAKE_BUILD_TYPE=Release
cmake --build build_unleash -j4
export DM="$FFP_ROOT/build_unleash/DeploymentMgr"
```

또는 ota-manager 쪽:

```bash
cd "$OTA_ROOT/platform/DeploymentManager"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
export DM="$OTA_ROOT/platform/DeploymentManager/build/DeploymentMgr"
```

---

## 2단계 — 데모 소스 확인

```bash
grep -E 'kDefaultVersion|kFeatureName|\[EV v3.0\]' "$OTA_ROOT/demo_src/EvEnergy_v3.0.cpp" | head -8
```

**기대:** `3.0`, `EvEnergyFeature`, `[EV v3.0]` 로그 문자열.

소스가 없으면:

```bash
cp "$OTA_ROOT/demo_src/EvEnergy_v2.0.cpp" "$OTA_ROOT/demo_src/EvEnergy_v3.0.cpp"
# 에디터로 kDefaultVersion = "3.0" 및 [EV v3.0] 로그 추가
# (저장소에는 Navigation v3.0 패턴으로 EvEnergy_v3.0.cpp 가 이미 있음)
```

---

## 3단계 — 패키지 스켈레톤

```bash
cd "$OTA_ROOT"
./scripts/new_package.sh --domain Energy --feature EvEnergyFeature --version 3.0
```

생성 위치: `platform/deploy/packages/Energy/v3.0/` (Dockerfile, manifest.json, README)

---

## 4단계 — 빌드 (register 전 필수)

Candidate **v3.0** 과 Primary용 **v2.0** 둘 다 빌드합니다.

```bash
cd "$OTA_ROOT"
./scripts/build_standalone_demo.sh --domain EvEnergy --version 2.0
./scripts/build_standalone_demo.sh --domain EvEnergy --version 3.0

test -f build_artifacts/evenergy/v3.0/EvEnergyFeature
test -f build_artifacts/evenergy/v3.0/Dockerfile
test -f build_artifacts/evenergy/v2.0/EvEnergyFeature
```

이미지 태그:

- `sdv/evenergy-feature:v2.0`
- `sdv/evenergy-feature:v3.0`

---

## 5단계 — v3.0 이미지 동작 확인

```bash
docker run --rm sdv/evenergy-feature:v3.0 /app/EvEnergyFeature 3.0
```

**기대:** `Version: 3.0`, `[EV v3.0]` 로그 → Ctrl+C.

(무한 루프만 보고 싶지 않으면):

```bash
docker run --rm sdv/evenergy-feature:v3.0 /app/EvEnergyFeature 3.0 2>&1 | head -8
```

---

## 6단계 — local registry + v3.0 push

```bash
docker ps | grep local-registry || \
  docker run -d -p 5000:5000 --restart unless-stopped --name local-registry registry:2

docker tag sdv/evenergy-feature:v3.0 localhost:5000/sdv/evenergy-feature:v3.0
docker push localhost:5000/sdv/evenergy-feature:v3.0
```

---

## 6b단계 — (필수) Primary용 v2.0도 registry에

Shadow 배포 전 **Primary 컨테이너**가 필요합니다. car_10 Energy는 **v2.0 primary** 기준입니다.

```bash
if ! docker image inspect sdv/evenergy-feature:v2.0 >/dev/null 2>&1; then
  cd "$OTA_ROOT"
  test -f demo_src/EvEnergy_v2.0.cpp || echo "EvEnergy_v2.0.cpp 필요"
  ./scripts/build_standalone_demo.sh --domain EvEnergy --version 2.0
fi

docker tag sdv/evenergy-feature:v2.0 localhost:5000/sdv/evenergy-feature:v2.0
docker push localhost:5000/sdv/evenergy-feature:v2.0
```

---

## 7단계 — 패키지 등록

```bash
cd "$OTA_ROOT"
./scripts/register_build_artifact.sh \
  --domain Energy \
  --version 3.0 \
  --feature EvEnergyFeature \
  --source build_artifacts/evenergy/v3.0

ls -la "$PLATFORM_ROOT/deploy/packages/Energy/v3.0/"
```

---

## 8단계 — manifest + version_map (slot B = v3.0)

```bash
cd "$OTA_ROOT"
python3 <<'PY'
import json
from pathlib import Path

manifest_p = Path("platform/deploy/packages/Energy/v3.0/manifest.json")
m = json.loads(manifest_p.read_text(encoding="utf-8"))
m["docker_image"] = "localhost:5000/sdv/evenergy-feature:v3.0"
m["version"] = "3.0"
m["domain"] = "Energy"
m["feature"] = "EvEnergyFeature"
manifest_p.write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8")

vm_p = Path("platform/deploy/version_map.json")
d = json.loads(vm_p.read_text(encoding="utf-8"))
d["features"]["Energy"]["B"] = {
    "version": "v3.0",
    "role": "candidate",
    "binary": "EvEnergyFeature",
    "package_id": "Energy_v3.0",
    "package_path": "deploy/packages/Energy/v3.0",
}
vm_p.write_text(json.dumps(d, indent=2) + "\n", encoding="utf-8")
print("OK Energy B=v3.0")
PY
```

---

## 9단계 — rollout policy

저장소에 파일이 있습니다 (재생성하려면):

```bash
cat > "$PLATFORM_ROOT/deploy/rollout_policies/policy_single_car_candidate_manual_promote_energy_v3.0.json" <<'EOF'
{
  "schema_version": 1,
  "policy_id": "policy_single_car_candidate_manual_promote_energy_v3.0",
  "name": "Single car candidate deploy then manual promote for Energy v3.0",
  "description": "Deploy Energy v3.0 as candidate to one vehicle, verify logs, then manually promote to primary.",
  "stages": [
    {
      "stage_id": "candidate_deploy",
      "stage_name": "candidate",
      "deploy_action": "candidate",
      "require_ota_ready": true,
      "docker_runtime": { "enabled": true, "pull_policy": "missing" },
      "success_criteria": {
        "docker_runtime_result": "success",
        "expected_log_patterns": ["[EV v3.0]", "Candidate Shadow version is running"]
      }
    },
    {
      "stage_id": "manual_approval",
      "stage_name": "approval",
      "action": "wait_for_manual_approval",
      "required": true
    },
    {
      "stage_id": "primary_promote",
      "stage_name": "promote",
      "deploy_action": "promote",
      "require_ota_ready": true,
      "docker_runtime": { "enabled": true, "pull_policy": "missing" },
      "success_criteria": {
        "docker_runtime_result": "success",
        "expected_log_patterns": ["[EV v3.0]", "Primary production path running"]
      }
    }
  ],
  "approval_required": true,
  "manual_promote_required": true
}
EOF
```

---

## 10단계 — 캠페인 (car_10)

저장소: `platform/deploy/campaigns/campaign_energy_v3.0_car10_canary.json`

대시보드 **TARGET: vehicle: car_10** (JSON에 `vehicle_ids: ["car_10"]` 포함)

---

## 11단계 — car_10 Energy Primary bootstrap (Shadow 전 필수)

```bash
cd "$OTA_ROOT"
docker rm -f car_10_agent_evenergy car_10_agent_evenergy_shadow 2>/dev/null || true

./scripts/ota_vehicle.sh bootstrap --car car_10 --domain EvEnergy --version 2.0 \
  --image localhost:5000/sdv/evenergy-feature:v2.0 --pull missing

docker ps --filter name=car_10_agent_evenergy --format "table {{.Names}}\t{{.Status}}\t{{.Image}}"
```

**기대:** `car_10_agent_evenergy` 가 **Up**, 이미지 `localhost:5000/sdv/evenergy-feature:v2.0`

---

## 12단계 — car_10 OTA READY (Run Candidate 전 필수)

```bash
cd "$OTA_ROOT"
python3 <<'PY'
import json
from pathlib import Path

car = "car_10"
state_p = Path("platform/deploy/state") / f"{car}.json"
s = json.loads(state_p.read_text(encoding="utf-8"))
s["ota"].update({
    "update_mode": "READY",
    "download_status": "downloaded",
    "install_status": "not_started",
    "driver_approved": True,
    "battery": 82,
})
s["features"]["Energy"].update({
    "kill_switch": False,
    "flag": "ON",
})
state_p.write_text(json.dumps(s, indent=2) + "\n", encoding="utf-8")
print("OK", car, "OTA READY (Energy)")
PY
```

---

## 13단계 — campaign validate

```bash
cd "$PLATFORM_ROOT"
"$DM" campaign-validate deploy/campaigns/campaign_energy_v3.0_car10_canary.json
```

**기대:** `fail=0`, 타깃 car_10 1대.

---

## 14단계 — (Windows) 플랫폼 재기동

```powershell
cd "C:\Users\82108\Desktop\teamproject\sdv-feature-flag-platform"
.\run_sdv.bat
```

대시보드 Refresh → **campaign_energy_v3.0_car10_canary** 표시 확인.

---

## 15단계 — (대시보드) Candidate 배포

1. 캠페인 **campaign_energy_v3.0_car10_canary** 선택  
2. **Prepare Demo READY** → Demo Ready: YES  
3. **Validate** → fail=0  
4. **Run Candidate**

성공 확인 (WSL):

```bash
docker ps --filter name=car_10_agent_evenergy --format "table {{.Names}}\t{{.Status}}\t{{.Image}}"

grep -E 'shadow_enabled|shadow_version|docker_runtime_result' \
  "$OTA_ROOT/platform/deploy/state/car_10.json" | grep -A0 -B0 Energy || \
grep -E '"Energy"|shadow_enabled|shadow_version|docker_runtime' \
  "$OTA_ROOT/platform/deploy/state/car_10.json" | head -20
```

**기대:**

- `car_10_agent_evenergy` (v2.0) + `car_10_agent_evenergy_shadow` (v3.0) 둘 다 **Up**
- Energy: `shadow_enabled: true`, `shadow_version: v3.0`
- `docker_runtime_result: success`

Shadow 로그:

```bash
docker logs --tail 30 car_10_agent_evenergy_shadow 2>&1 | grep -E 'EV v3.0|Candidate Shadow'
```

최신 run:

```bash
ls -t "$PLATFORM_ROOT/deploy/campaign_runs/"*energy*v3*candidate*.json 2>/dev/null | head -1 | xargs grep -E '"status"|docker_runtime_result|failures'
```

---

## 16단계 — (대시보드) Promote Primary

1. Shadow 로그에 `[EV v3.0] Candidate Shadow...` 확인  
2. **Prepare Demo READY** (배포 후 `update_mode`가 COMPLETED일 수 있음)  
3. **Validate**  
4. **Promote Primary**

Promote 후 확인:

```bash
grep -E 'primary_version|shadow_enabled|docker_runtime_result' \
  "$OTA_ROOT/platform/deploy/state/car_10.json" | head -12

docker logs --tail 20 car_10_agent_evenergy 2>&1 | grep -E 'v3.0|Primary production|\[EV v3.0\]' || true
```

**기대:** Energy `primary_version: v3.0`, Primary 로그에 `[EV v3.0] Primary production path running`

---

## 한 번에 1~13단계 (WSL)

```bash
bash "$OTA_ROOT/scripts/run_evenergy_v3_build_steps.sh"
```

(이미지 빌드·registry push·register·bootstrap·validate까지)

---

## 주의 (ADAS 시나리오와 다른 점)

| 항목 | 설명 |
|------|------|
| `new_package --domain Energy` | manifest 기본 이미지명이 `sdv/energy-feature`일 수 있음 → **8단계에서 `localhost:5000/sdv/evenergy-feature:v3.0`로 반드시 수정** |
| `build_standalone --domain EvEnergy` | 이미지는 `sdv/evenergy-feature` (소문자 evenergy) |
| Campaign `feature` | JSON 필드는 **`Energy`** (deploy/state의 `features.Energy`) |
| `ota_vehicle.sh --domain` | **`EvEnergy`** |
