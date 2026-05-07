## 목적

이 문서는 교수님 피드백(“정적 구조 vs 동적 실행 흐름을 분리해서 설명”)에 맞춰,
DeploymentManager 기반 배포관리(OTA) 아키텍처를 **다이어그램 종류별로 나눠** 발표/보고서/EA에 옮길 수 있게 정리합니다.

중요:
- 이 문서는 **설명/가이드 문서**이며 코드 동작을 바꾸지 않습니다.
- DeploymentManager는 Vehicle Plane의 HPC 안에서 **Feature Flag Platform과 같은 HPC 내 독립 모듈**로 설명합니다(플랫폼 내부 컴포넌트가 아님).

관련 문서:
- `README_DEPLOY_LAYOUT.md` (state/rollback/history/layout + Primary/Shadow/ACK/online 개념)
- `README_DOCKER_PACKAGES.md` (manifest/registry/메타데이터)
- `README_PROGRESSIVE_ROLLOUT.md` (canary→beta→stable 운영 흐름)
- `README_VEHICLE_ONLINE_STATUS.md` (registered vs online 기준)
- `sdv-feature-flag-platform/Docs/SHADOW_DEMO_SCENARIO.md` (플랫폼 프로세스 기반 Shadow 시나리오)

---

## 1) 정적 구조(Static Structure) — 무엇이 어디에 있고, 책임이 무엇인가

정적 구조 설명은 **“모듈 경계/책임/배치 위치”**를 보여주는 다이어그램으로 나눕니다.

### A. Component Diagram (추천)

목표: DevOps/Cloud ↔ Vehicle Plane(HPC) 간 모듈과 책임 경계를 한 장에 표시.

포함할 컴포넌트(권장):

- **DevOps/Cloud**
  - Package build (Docker image build / CI)
  - Quality validation (테스트/스캔/서명/승인)
  - Deployment command 발행(캠페인/rollout 정책)

- **Vehicle Plane / HPC**
  - **DeploymentManager (DM)**: 패키지 수신/검증/배치/상태+이력 기록
  - **Feature Flag Platform**: EventBus / Evaluator / signal I/O / data collector
  - **LifeCycleManager / Vehicle Runtime**: state/active pointer를 읽어 실제 기능 실행(컨테이너/프로세스)
  - **Feature Docker Containers**: ADAS / Navigation / Energy (Primary/Shadow role)

명시할 핵심 문장:
- **DeploymentManager는 Feature Flag Platform 내부가 아니라, 같은 HPC 안의 독립 모듈이다.**

### B. Allocation Diagram (Deployment/Physical) (추천)

목표: “어떤 소프트웨어가 어떤 노드/환경에 배치되는지”.

- Cloud/DevOps node
- Vehicle HPC node
  - DM 프로세스(또는 서비스)
  - Platform 런타임(Feature Flag Platform + LCM)
  - Docker runtime + feature containers
- 저장소(파일 기반):
  - `deploy/packages`, `deploy/state`, `deploy/rollback`, `deploy/history`

### C. Class Diagram (보조)

목표: DM 내부 구조를 “클래스 책임”으로만 간단히.

DeploymentManager 모듈 내부(권장 포함):
- `DeploymentManager`
- `StateManager`
- `VersionManager`
- `VehicleRegistry`
- `DeploymentHistory`
- `ConfigUpdater`
- `PackageInfo`

주의:
- Class diagram에서는 “시퀀스/흐름”을 설명하지 않고, **역할(책임)과 데이터 소유만** 표시합니다.

---

## 2) 동적 실행 흐름(Dynamic Runtime Flow) — 어떤 이벤트가 어떤 순서로 일어나는가

동적 흐름은 **Sequence Diagram / Runtime Flow**로 설명합니다.

### A. Sequence Diagram: Shadow 검증 → Promote 또는 차단/격리

참여자(예시):
- DevOps/Operator
- DeploymentManager (HPC)
- Registry/Manifest store (`deploy/packages`, `package_registry.json`)
- Vehicle Runtime (LCM/Docker runtime)
- Feature Primary container
- Feature Shadow container
- State store (`deploy/state/<car>.json`)
- History store (`deploy/history/fleet_history.jsonl`)

필수 단계(문서/발표에서 순서로 포함):
1. Docker 이미지 패키지 등록(manifest/registry)
2. 대상 차량/그룹 선택(vehicle/group/region)
3. 이미지 확보(build/pull) (이 단계는 DM이 직접 수행하지 않을 수 있음 — 환경에 따라)
4. checksum/image digest 검증
5. OTA 안전조건 확인(driver_approved, update_mode=READY 등)
6. Shadow에 먼저 배치(Primary 유지)
7. Shadow 검증(로그/ACK/메트릭)
8. 검증 성공 시 Promote(Shadow→Primary)
9. 검증 실패 시 blocked/quarantine 기록(Primary 유지; rollback 아님)
10. Primary 문제 발생 시 rollback_stack 기준 Rollback
11. history 기록
12. ACK 수신 및 `last_seen_at` 갱신(online 판단 근거)

Mermaid 예시(개념용):

```mermaid
sequenceDiagram
  autonumber
  actor Ops as DevOps/Operator
  participant DM as DeploymentManager (HPC)
  participant REG as packages/registry (manifest)
  participant ST as deploy/state/<car>.json
  participant RT as Vehicle Runtime (LCM/Docker)
  participant SH as Shadow container
  participant PR as Primary container
  participant H as deploy/history/fleet_history.jsonl

  Ops->>DM: rollout command (target=canary, feature=Navigation, version=v2.0)
  DM->>REG: read manifest/registry (docker_image, digest, policy)
  DM->>ST: check OTA safety gate (driver_approved, update_mode=READY)
  DM->>RT: deploy-shadow (place candidate to Shadow; Primary unchanged)
  RT->>SH: start candidate image (Shadow)
  SH-->>DM: validation signals/ACK (passed/failed)
  DM->>ST: update shadow_status + last_ack + last_seen_at
  alt validation passed
    Ops->>DM: promote approve
    DM->>RT: promote Shadow -> Primary
    RT->>PR: switch Primary to new image
    DM->>ST: update primary_version + rollback_stack
  else validation failed
    DM->>ST: record quarantine/blocked (shadow_validation_failed)
    Note over DM,PR: Primary stays unchanged (not a rollback)
  end
  DM->>H: append fleet history record
```

### B. Runtime Flow (텍스트/Activity) (추천)

목표: 시퀀스 다이어그램이 너무 복잡할 때, “상태 전이” 관점으로 요약.

정리 기준(추천 키):
- OTA phase: `ota.ota_phase`
- Shadow 상태: `shadow_status`, `shadow_validation_result`
- ACK: `last_ack`
- Online: `last_seen_at`
- Rollback: `rollback_stack`, `blocked_versions`, `quarantine`

---

## 3) 교수님께 설명할 때 주의해야 할 표현(혼동 방지)

- **“DeploymentManager가 Feature를 실행한다”** 라고 말하지 않기
  - DM은 “패키지/메타데이터/상태”를 준비하고 기록하는 역할
  - 실제 실행/전환은 Vehicle Runtime(LCM/Docker)이 담당

- **Shadow 실패는 rollback이 아니다**
  - Shadow는 Primary를 바꾸지 않는 Candidate 검증 단계
  - 실패 시 Primary 유지 + blocked/quarantine 기록

- **ACK vs OTA phase를 섞지 않기**
  - `last_ack`: 차량(또는 데모 CLI) 응답 기록
  - `ota_phase`: 배포관리 진행 상태 기록

- **registered vs online을 분리**
  - registered: vehicles.json
  - online: last_seen 기반(ACK/heartbeat/probe)

---

## 4) EA(Enterprise Architect)로 옮길 때의 추천 다이어그램 세트

- 1장: Component Diagram (Cloud ↔ HPC, DM vs Platform 경계 강조)
- 1장: Allocation/Deployment Diagram (배치 위치 + 파일 저장소)
- 1장: Sequence Diagram (Shadow→Validate→Promote/Quarantine→Rollback)
- (옵션) 1장: Class Diagram (DM 내부 클래스 관계 요약)

