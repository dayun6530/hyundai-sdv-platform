# ADAS v6.0 · car_2 데모 — 순서대로 (빠짐없음)

**목표:** `campaign_adas_v6.0_car2_canary` → Validate `fail=0` → Shadow v6.0 → Promote Primary v6.0  
**대상 차량:** `car_2` only  
**이미 이 PC에서 준비 스크립트를 돌렸다면** → [§2 데모 당일](#2-데모-당일-10분)만 따르면 됩니다.

---

## 0. 두 JSON 파일 (헷갈리면 여기만 본다)

| 파일 | 역할 |
|------|------|
| `platform/deploy/state/car_2.json` | OTA `update_mode`, ADAS `flag`, shadow 슬롯, 배포 이력 |
| `deploy/vehicle_runtime/car_2.json` | Docker에 **실제로** 떠 있는 `primary_version` / `shadow_version` (CMD 로그에 표시) |

**Validate 실패의 90%:** 캠페인 `target_version`(v6.0) ≠ `version_map.json` → ADAS → **B** 버전.

---

## 1. 사전 준비 (한 번만, WSL)

### 1-A. 한 줄 자동 준비 (권장)

PowerShell에서 WSL로 실행 (**`bash`로 실행** — Windows CRLF 때문에 `./script.sh`가 깨질 수 있음):

```powershell
wsl -e bash -lc "sed -i 's/\r$//' /mnt/c/Users/USER/Desktop/feature-flag-platform3/sdv-ota-manager/scripts/demo_setup_adas_v6_car2.sh && cd /mnt/c/Users/USER/Desktop/feature-flag-platform3/sdv-ota-manager && bash ./scripts/demo_setup_adas_v6_car2.sh"
```

**끝에 반드시 보여야 함:** `[SUMMARY] ok=12 warn=0 fail=0`

### 1-B. 수동 단계 (자동 스크립트가 안 될 때만)

아래 순서를 **건너뛰지 말 것**. `register` 전에 **반드시** `build`가 있어야 함.

```bash
export ROOT=/mnt/c/Users/USER/Desktop/feature-flag-platform3
export OTA=$ROOT/sdv-ota-manager
export PLATFORM=$ROOT/sdv-feature-flag-platform
export DM=$PLATFORM/build_unleash/DeploymentMgr
cd $OTA

# (1) 소스
cp -f demo_src/Adas_v0.5.cpp demo_src/Adas_v6.0.cpp
sed -i 's/v0\.5/v6.0/g; s/0\.5/6.0/g' demo_src/Adas_v6.0.cpp

# (2) 패키지 골격
./scripts/new_package.sh --domain ADAS --feature AdasFeature --version 6.0

# (3) 빌드 — 없으면 register 실패!
./scripts/build_standalone_demo.sh --domain ADAS --version 6.0

# (4) register
./scripts/register_build_artifact.sh --domain ADAS --version 6.0

# (5) manifest + version_map B=v6.0 (python3 블록은 demo_setup 스크립트 참고)

# (6) 레지스트리
docker start local-registry 2>/dev/null || docker run -d -p 5000:5000 --restart unless-stopped --name local-registry registry:2
docker tag sdv/adas-feature:v6.0 localhost:5000/sdv/adas-feature:v6.0
docker push localhost:5000/sdv/adas-feature:v6.0

# (7) Validate
cd $OTA/platform
$DM campaign-validate deploy/campaigns/campaign_adas_v6.0_car2_canary.json
# → fail=0 확인
```

### 1-C. 사전 준비 체크리스트

- [ ] `platform/deploy/version_map.json` → ADAS **B** = **v6.0**
- [ ] `platform/deploy/packages/ADAS/v6.0/manifest.json` → `docker_image`: `localhost:5000/sdv/adas-feature:v6.0`
- [ ] `docker images`에 `localhost:5000/sdv/adas-feature:v6.0` 존재
- [ ] `platform/deploy/state/car_2.json` → ADAS `"flag": "ON"`, OTA `"update_mode": "READY"`
- [ ] `$DM campaign-validate ...v6.0_car2...` → **fail=0**

---

## 2. 데모 당일 (10분)

### 2-1. 플랫폼 기동 (Windows)

```powershell
cd C:\Users\USER\Desktop\feature-flag-platform3\sdv-feature-flag-platform
.\run_sdv.bat
```

또는 **car_2만** 띄울 때:

```powershell
.\scripts\start_car.ps1 -Car 2
```

브라우저: **http://127.0.0.1:5173**  
(status-server **8080** — `run_sdv`가 같이 올림)

> 코드 수정 후(`validateId`, FleetVersionResolve 등)에는 **run_sdv를 완전히 끄고 다시 시작**.

### 2-2. 대시보드 UI (순서 고정)

**Deployment Campaigns** 패널:

| # | 버튼 | 성공 기준 |
|---|------|-----------|
| 1 | 캠페인 선택 | **`campaign_adas_v6.0_car2_canary`** (기본 선택됨) |
| 2 | **Validate** | `fail=0` (fail=2면 version_map B ≠ v6.0) |
| 3 | **Safety Preconditions** → **Prepare Demo READY** | `car_2` OTA READY |
| 4 | **Run Candidate** | Run History에 candidate **success** |
| 5 | **Candidate Logs** | `[ADAS v6.0]`, `Candidate Shadow version is running` |
| 6 | (필요 시) **Prepare Demo READY** 다시 | Candidate 후 `update_mode`가 COMPLETED면 |
| 7 | **Promote Primary** | promote **success** |
| 8 | **Primary Logs** | `[ADAS v6.0]`, `Primary production path running` |
| 9 | **Vehicle Runtime Summary** | `primary_version`: v6.0, shadow 비활성 또는 정리됨 |

### 2-3. CMD / Fleet 로그에서 볼 문장

LifeCycleMgr 틱마다 **vehicle_runtime** 기준:

- Candidate: `primary=v1.0 shadow=v6.0` (또는 유사)
- Promote 후: `primary=v6.0`

Docker 직접 확인 (WSL):

```bash
docker logs -f car_2_agent_adas_shadow   # Candidate
docker logs -f car_2_agent_adas          # Primary (Promote 후)
```

---

## 3. UI 대신 CLI (백업)

```bash
export ROOT=/mnt/c/Users/USER/Desktop/feature-flag-platform3
export DM=$ROOT/sdv-feature-flag-platform/build_unleash/DeploymentMgr
cd $ROOT/sdv-ota-manager/platform

$DM campaign-validate deploy/campaigns/campaign_adas_v6.0_car2_canary.json
$DM campaign-run deploy/campaigns/campaign_adas_v6.0_car2_canary.json --phase candidate
# OTA READY 확인 후
$DM campaign-run deploy/campaigns/campaign_adas_v6.0_car2_canary.json --phase promote
```

---

## 4. 실수하면 점수 깎이는 것 (피하기)

| 증상 | 원인 | 해결 |
|------|------|------|
| Validate **fail=2** | 캠페인 v6.0인데 version_map B가 v4.0 등 | `demo_setup` 다시 또는 B=v6.0 수동 |
| `Source build_artifacts ... not found` | **build 없이 register** | `build_standalone_demo.sh` 먼저 |
| Run Candidate 실패 `not READY` | OTA 상태 | **Prepare Demo READY** |
| 로그 `[SKIP]` | ADAS `flag: OFF` | `state/car_2.json` → `"flag": "ON"` |
| Promote 비활성 | candidate run 없음 | 4번 Run Candidate 먼저 |
| `validateId is not a function` | status-server 구버전 | run_sdv 재시작 |
| `./demo_setup...sh` bash\r 오류 | CRLF | `bash ./scripts/demo_setup_adas_v6_car2.sh` |

---

## 5. 생성된 파일 목록

- `demo_src/Adas_v6.0.cpp`
- `platform/deploy/campaigns/campaign_adas_v6.0_car2_canary.json`
- `platform/deploy/rollout_policies/policy_single_car_candidate_manual_promote_v6.0.json`
- `scripts/demo_setup_adas_v6_car2.sh`

---

## 6. 데모 후 정리 (선택)

```bash
docker rm -f car_2_agent_adas car_2_agent_adas_shadow
```

다음 버전 데모 전에 같은 `demo_setup` 패턴으로 B 슬롯만 바꾸면 됩니다.
