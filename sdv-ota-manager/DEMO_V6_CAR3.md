# ADAS v6.0 · car_3 데모 — 처음부터 끝까지

**버전:** `v6.0` (폴더 `v6.0` / 이미지 `v6.0`) — **`v0.6` 아님**  
**대상 차량:** `car_3`  
**캠페인:** `campaign_adas_v6.0_car3_canary`

---

## 0. (Windows) 플랫폼 기동

```powershell
cd C:\Users\USER\Desktop\feature-flag-platform3\sdv-feature-flag-platform
.\run_sdv.bat
```

브라우저: **http://127.0.0.1:5173**

> `run_sdv.bat`이 status-server + dashboard + car_1~car_10 CMD를 띄웁니다.  
> **car_3 CMD**에서 `version_events.log` tail이 보이면 대시보드 배포가 실시간 반영됩니다.

---

## 1. (WSL) 경로 변수

**이미 WSL 안이면 `wsl` 명령 쓰지 마세요.**

```bash
export ROOT=/mnt/c/Users/USER/Desktop/feature-flag-platform3
export OTA_ROOT=$ROOT/sdv-ota-manager
export PLATFORM_ROOT=$OTA_ROOT/platform
export FFP_ROOT=$ROOT/sdv-feature-flag-platform
export DM=$FFP_ROOT/build_unleash/DeploymentMgr

cd $OTA_ROOT
```

---

## 2. v6.0 데모 소스 확인·생성

```bash
ls -la $OTA_ROOT/demo_src | grep -i Adas

cp -f $OTA_ROOT/demo_src/Adas_v0.5.cpp $OTA_ROOT/demo_src/Adas_v6.0.cpp
sed -i 's/v0\.5/v6.0/g; s/0\.5/6.0/g' $OTA_ROOT/demo_src/Adas_v6.0.cpp
grep '\[ADAS v6.0\]' $OTA_ROOT/demo_src/Adas_v6.0.cpp
```

---

## 3. ADAS v6.0 Docker image build

```bash
cd $OTA_ROOT
./scripts/build_standalone_demo.sh --domain ADAS --version 6.0

docker images | grep -i 'adas-feature.*v6.0' || true
```

---

## 4. v6.0 이미지 직접 실행 확인

```bash
docker run --rm sdv/adas-feature:v6.0 /app/AdasFeature 6.0
```

로그에 `[ADAS v6.0]` 가 보여야 합니다.

---

## 5. local registry

```bash
docker ps | grep local-registry || \
  docker run -d -p 5000:5000 --restart unless-stopped --name local-registry registry:2
```

---

## 6. v6.0 → local registry push

```bash
docker tag sdv/adas-feature:v6.0 localhost:5000/sdv/adas-feature:v6.0
docker push localhost:5000/sdv/adas-feature:v6.0
```

---

## 7. (선택) pull 테스트용 tag 삭제

```bash
docker rmi localhost:5000/sdv/adas-feature:v6.0 2>/dev/null || true
```

---

## 8. register (build 다음에만)

```bash
cd $OTA_ROOT
./scripts/register_build_artifact.sh --domain ADAS --version 6.0
ls -la $PLATFORM_ROOT/deploy/packages/ADAS/v6.0
```

---

## 9. manifest + version_map (B = v6.0)

```bash
cd $OTA_ROOT

python3 <<'PY'
import json
from pathlib import Path

manifest_p = Path("platform/deploy/packages/ADAS/v6.0/manifest.json")
m = json.loads(manifest_p.read_text(encoding="utf-8"))
m["docker_image"] = "localhost:5000/sdv/adas-feature:v6.0"
m["version"] = "6.0"
manifest_p.write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8")

vm_p = Path("platform/deploy/version_map.json")
d = json.loads(vm_p.read_text(encoding="utf-8"))
d["features"]["ADAS"]["B"] = {
    "version": "v6.0",
    "role": "candidate",
    "binary": "AdasFeature",
    "package_id": "ADAS_v6.0",
    "package_path": "deploy/packages/ADAS/v6.0",
}
vm_p.write_text(json.dumps(d, indent=2) + "\n", encoding="utf-8")
print("OK version_map ADAS B=v6.0")
PY
```

---

## 10. v6.0 policy (없으면 생성)

```bash
cd $PLATFORM_ROOT

test -f deploy/rollout_policies/policy_single_car_candidate_manual_promote_adas_v6.0.json || {
  cp deploy/rollout_policies/policy_single_car_candidate_manual_promote_adas.json \
     deploy/rollout_policies/policy_single_car_candidate_manual_promote_adas_v6.0.json
  python3 <<'PY'
import json
from pathlib import Path
p = Path("deploy/rollout_policies/policy_single_car_candidate_manual_promote_adas_v6.0.json")
d = json.loads(p.read_text(encoding="utf-8"))
d["policy_id"] = "policy_single_car_candidate_manual_promote_adas_v6.0"
text = json.dumps(d, indent=2).replace("v0.5", "v6.0").replace("0.5", "6.0")
p.write_text(text + "\n", encoding="utf-8")
PY
}
```

---

## 11. car_3 캠페인 (저장소에 있음)

파일: `platform/deploy/campaigns/campaign_adas_v6.0_car3_canary.json`

---

## 12. car_3 초기화 (데모 전)

```bash
docker rm -f car_3_agent_adas car_3_agent_adas_shadow 2>/dev/null || true

python3 <<'PY'
import json
from pathlib import Path

car = "car_3"
state_p = Path("platform/deploy/state") / f"{car}.json"
s = json.loads(state_p.read_text(encoding="utf-8"))
s["features"]["ADAS"].update({
    "active_slot": "A", "active_version": "v1.0",
    "primary_slot": "A", "primary_version": "v1.0",
    "primary_image": "sdv/adas-feature:v1.0",
    "shadow_enabled": False, "shadow_slot": "", "shadow_version": "",
    "shadow_mode": "inactive", "flag": "ON", "kill_switch": False,
})
s["ota"].update({
    "update_mode": "READY", "download_status": "downloaded",
    "install_status": "not_started", "driver_approved": True,
})
state_p.write_text(json.dumps(s, indent=2) + "\n", encoding="utf-8")

rt_p = Path("deploy/vehicle_runtime") / f"{car}.json"
r = json.loads(rt_p.read_text(encoding="utf-8"))
r["domains"]["adas"] = {
    "primary_version": "v1.0", "shadow_version": "",
    "binary": "AdasFeature",
    "primary_container": "car_3_agent_adas",
    "shadow_container": "car_3_agent_adas_shadow",
    "last_action": "reset",
    "last_message_ko": "car_3 ADAS demo reset before v6.0",
    "shadow_enabled": False, "shadow_mode": "inactive",
}
rt_p.write_text(json.dumps(r, indent=2) + "\n", encoding="utf-8")
print("OK car_3 reset")
PY
```

---

## 13. campaign validate (fail=0)

```bash
cd $PLATFORM_ROOT
"$DM" campaign-validate deploy/campaigns/campaign_adas_v6.0_car3_canary.json
```

`[SUMMARY] ... fail=0` 확인.

---

## 한 줄 자동 준비 (WSL)

```bash
cd /mnt/c/Users/USER/Desktop/feature-flag-platform3/sdv-ota-manager
sed -i 's/\r$//' ./scripts/demo_setup_adas_v6_car3.sh
bash ./scripts/demo_setup_adas_v6_car3.sh
```

---

## 14~19. UI (car_3)

| # | 동작 | 확인 |
|---|------|------|
| 14 | 캠페인 **`campaign_adas_v6.0_car3_canary`** | target car_3 |
| 15 | **Prepare Demo READY** | OTA READY |
| 16 | **Validate** | fail=0 |
| 17 | **Run Candidate** | success, `[ADAS v6.0]` shadow |
| 18 | **Prepare Demo READY** (다시) | COMPLETED 후 |
| 19 | **Promote Primary** | primary v6.0 |

---

## CMD 실시간 확인 (car_3)

```bash
tail -f $FFP_ROOT/logs/car_3/version_events.log
```

또는 **car_3** 제목 CMD 창에서:

```text
[VERSION] car_id=car_3 process=AdasFeature ... primary_version=... shadow_version=...
```

---

## Docker 로그

```bash
docker logs -f car_3_agent_adas_shadow   # Candidate
docker logs -f car_3_agent_adas          # Promote 후 Primary
```

---

## 실수 방지

| 증상 | 조치 |
|------|------|
| v0.6 / v0.6 폴더 사용 | **v6.0** 만 사용 (`--version 6.0`) |
| Validate fail=2 | version_map B = **v6.0** |
| register 실패 | **build** 먼저 |
| CMD에 버전 안 보임 | status-server 재시작 + `run_sdv.bat` |
| WSL에서 `wsl` not found | 이미 WSL 안 → `bash script.sh` 만 |
