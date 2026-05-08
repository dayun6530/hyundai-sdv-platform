## 목적

이 문서는 `sdv-ota-manager/platform/deploy/` 아래의 **배포 저장소 레이아웃**을 “Feature별 Docker 이미지 배포” 관점에서 설명합니다.

핵심 원칙:
- 이 저장소는 **Docker 이미지 파일(tar)** 을 직접 보관하는 곳이 아닙니다.
- 저장되는 것은 주로 **manifest / tag / digest / checksum / state / history / pointer** 같은 *메타데이터*입니다.
- 실제 Docker 이미지는 각 PC/환경에서 `docker build` / `docker pull` (또는 스크립트)로 확보합니다.

---

## 현재 폴더 구조 (repo 기준)

현재 이 리포에서 `platform/deploy/`는 주로 **상태(state)/이력(history)/롤백 메타데이터** 중심으로 사용 중입니다.
일부 “패키지/배치” 폴더는 레거시 실행파일 기반 흐름에 맞춰 `DeploymentManager`가 기대하는 구조이므로,
Docker 이미지 배포 관점에서도 **메타데이터/pointer 보관 위치**로 정의해 둡니다.

### `packages/` (권장, 패키지 등록 저장소)

- **의미**: Feature Docker package의 “정식 등록본”을 보관하는 위치
- **포함**: `Dockerfile`, `manifest.json`, `checksum.sha256` 등
- 예: `deploy/packages/ADAS/v2.0/manifest.json`

주의:
- 이 폴더 안의 Dockerfile/바이너리/체크섬은 **이미지 빌드 입력**입니다.
- 실제 이미지는 `docker build -t sdv/adas-feature:v2.0 .`로 생성됩니다.

### rollback_stack 기록 시점(중요)

- **즉시 적용(immediate)**: `fleet-deploy --no-defer-native`처럼 active/primary가 즉시 변경되는 경로에서는 그 시점에 이전 primary를 `rollback_stack`에 push합니다.
- **지연 적용(deferred / pending_active_*)**: `fleet-deploy vehicle` 기본 모드처럼 pending으로 예약된 경우,
  **실제 적용 시점은** `apply-pending-native-car`(또는 차량 재시작 시점)입니다.
  따라서 `apply-pending-native-car`에서 “이전 primary → rollback_stack” 기록이 일어나는 것이 정확합니다.
- **Docker promote 연동(메타데이터)**: Docker runtime의 `ota_vehicle.sh promote`는 컨테이너를 승격하지만,
  DM의 `deploy/state` 메타데이터(rollback_stack/primary_image 등)는 별도 동기화가 필요할 수 있습니다.
  이 단계에서는 `DeploymentMgr fleet-promote-shadow vehicle <car_id> <FEATURE>`로 state만 승격/정리합니다(컨테이너 동작 없음).

### `download_tmp/` (권장, 임시 다운로드/수신)

- **의미**: 다운로드/수신 중인 임시 패키지(부분 파일 포함)
- **원칙**: 검증/등록이 끝나기 전에는 `packages/`/`staging/`로 승격하지 않음
- 실패 시: 해당 엔트리(폴더)를 정리(clean)하는 대상

### `staging/` (권장, 검증 완료 후 설치 대기)

- **의미**: 검증(체크섬/서명/정책) 완료 후, “활성화(전환) 전” 대기 상태
- Docker 이미지 기준으로는 이미지 자체가 아니라,
  - `docker_image` tag/digest,
  - 검증 결과,
  - 전환에 필요한 pointer
  등을 보관하는 폴더로 사용

### `active/` (권장, 현재 사용 가능한 활성 포인터)

- **의미**: 현재 Primary로 사용 가능한 “활성 포인터”
- 레거시 실행파일 기준: `deploy/active/<exec>` 위치에 바이너리 존재
- Docker 이미지 기준(권장): “현재 활성 이미지(tag/digest)”를 가리키는 pointer/metadata를 보관

### `backup/` (권장, 이전 active 보관)

- **의미**: 이전 active의 백업(레거시 실행파일) 또는 “이전 활성 포인터” 보관
- Docker 이미지 기준: 이전 `active` pointer의 스냅샷(메타데이터) 보관이 주 목적

---

## 상태/정책 저장소 (현재 이미 사용 중)

### `state/`
- **의미**: 차량별 fleet 상태 JSON
- 예: `deploy/state/car_1.json`

### `rollback/`
- **의미**: 차량별/Feature별 rollback 메타데이터(JSON)
- 예: `deploy/rollback/car_1/ADAS.json`

### `history/`
- **의미**: 배포/실패/롤백 이력 (예: `fleet_history.jsonl`)

### `metadata/`
- **의미**: 인벤토리/검증/보조 메타데이터(예: `inventory_*.state`)

### `stacks/`
- **의미**: 레거시 선택 스택(rollback stack) 관련 파일
- 이번 단계에서는 구조/구현을 변경하지 않음(10단계에서 다룸)

---

## 실패 시 정리 정책 (문서화)

아래 정책은 “Docker 이미지 배포 단위”를 전제로 한 운영 원칙이며, 이번 단계에서는 **로직 구현 변경 없이** 문서로만 명확히 합니다.

- **다운로드 실패**
  - `download_tmp/<package_id or domain/version>/`를 정리
  - `packages/`/`staging/`/`active/`로 승격 금지

- **checksum 실패 / image digest 검증 실패**
  - 해당 패키지를 `staging/`/`active/`로 올리지 않음
  - 실패 기록은 `history/`에 남김(원인 포함)

---

## 검증(verification) 수준 (중요)

현재 단계의 검증은 **metadata-level verification** 입니다.

- **현재 단계(메타데이터 수준)**:
  - `deploy/packages/<DOMAIN>/.../manifest.json` 존재 여부(있을 때만 Docker package로 판단)
  - `manifest.json`의 `docker_image`/`checksum_file`/`image_digest` 같은 필드 존재 여부
  - `checksum_file`이 가리키는 파일 존재 여부(있으면 통과, 없으면 실패 조건)
  - `image_digest`는 있으면 “기록된 것으로 인정”하지만, 원격 레지스트리와 비교하지 않음
- **후속 단계(미구현)**:
  - 실제 Docker registry/RepoDigest 기반의 digest pinning/검증
  - pull/push 및 signature(서명) 검증

`DeploymentManager`의 `fleet-deploy`는 이 검증 결과를 `deploy/state/<car>.json`의 `ota.ota_phase`로 기록합니다
(`VERIFYING` → `VERIFIED` 또는 `CHECKSUM_FAILED`).

- **Shadow 검증 실패 (관측 lane 실패)**
  - Shadow 대상은 “활성 전환”에서 제외
  - blocked/quarantine 후보로 기록(향후 확장: `metadata/quarantine/` 또는 `history` 레코드로 표시)

- **Primary 문제 발생**
  - (향후) rollback_stack 기준으로 이전 안정 버전으로 복구
  - 이번 단계에서는 rollback_stack 구현/구조 변경은 하지 않음

---

## ACK vs OTA phase (중요)

`deploy/state/<car>.json`에는 두 종류의 “진행/결과”가 공존할 수 있습니다.

- **`ota.*` (Deployment-manager tracked)**
  - `ota.download_status`, `ota.install_status`, `ota.update_mode`: 레거시 호환용 진행 상태
  - `ota.ota_phase`, `ota.failure_reason`: 배포관리자가 기록하는 “단계(phase)” 및 실패 사유
- **`last_ack` (Vehicle-reported)**
  - 차량이 “다운로드 완료/설치 실패…” 등을 **ACK로 보고**한 마지막 레코드
  - 이 단계에서는 파일/CLI 기반(`DeploymentMgr fleet-ack ...`)으로만 기록하며, MQTT/HTTP 전송은 추후 단계에서 구현

---

## 용어 매핑: Primary/Shadow vs legacy active/A/B (중요)

팀의 Docker 이미지 배포 기준 용어는 **Primary/Shadow** 입니다. 다만 기존 UI/API/CLI 호환을 위해
`deploy/state/<car>.json`에는 레거시 필드(`active_*`, `shadow_*`, A/B slot)가 남아 있습니다.

- **Primary**
  - 의미: 현재 실제 제어에 사용되는 버전(현재 “운영” 버전)
  - state 매핑:
    - `features.<F>.primary_version` (권장) ⇔ `features.<F>.active_version` (레거시 alias)
    - `features.<F>.primary_slot` (레거시 A/B) ⇔ `features.<F>.active_slot` (레거시 alias)
    - `features.<F>.primary_image` (선택; Docker image ref/tag)
- **Shadow**
  - 의미: 후보 버전(비교/검증용)으로 실행되거나 관측되지만 “제어”에는 사용하지 않는 버전
  - state 매핑:
    - `features.<F>.shadow_version` (그대로 유지)
    - `features.<F>.shadow_slot` (레거시 A/B; 필요 시만 사용)
    - `features.<F>.shadow_image` (선택; Docker image ref/tag)

주의(혼동 방지):
- **A/B slot은 “Primary/Shadow”의 반대 개념이 아닙니다.**
  - A/B는 version_map 내부의 레거시 slot(파일/아티팩트 선택)을 표현하는 키에 가깝고,
    Primary/Shadow는 “런타임 역할(role)”입니다.
- `stable/candidate`는 version_map/패키지 정책 용어로 남길 수 있지만,
  runtime/state 설명에서는 **Primary/Shadow를 우선**으로 해석합니다.

---

## Shadow Mode = Candidate 검증 흐름 (중요)

Shadow는 단순한 ON/OFF 기능이 아니라, **새 후보 Docker 이미지를 Primary로 승격(promote)하기 전에**
차량에서 **동일 입력 기반으로 로그/비교 검증**을 수행하는 “Candidate 검증 단계”입니다.

- **deploy-shadow**
  - 의미: Shadow 컨테이너(또는 Shadow 실행 경로)만 후보 이미지로 교체하여 검증 시작
  - 보장: **Primary는 변경되지 않음**
  - 실패: Shadow 후보는 **blocked/quarantine 대상으로 기록**(Primary 유지)
- **promote**
  - 의미: Shadow 검증 성공 시 Shadow 후보를 Primary로 승격하여 “운영”에 반영
  - 실패: promote 실패는 Primary 문제(primary_runtime_failed)와 구분되어야 하며,
    Shadow 후보 자체는 다시 promote 대상이 되지 않도록 blocked/quarantine 후보가 됨

상태(state) 표현(호환):
- 레거시: `shadow_enabled`, `shadow_slot`, `shadow_version`
- 보조(권장): `shadow_status`, `shadow_started_at`, `shadow_validation_result`, `shadow_failure_reason`, `shadow_image`

---

## registered vs online vehicles (fleet 관점)

대시보드에서 “차량 수”를 고정값처럼 보이지 않게 하려면, **등록(registered)** 과 **온라인(online)** 을 분리해야 합니다.

- **registered**: `config/fleet/vehicles.json`에 등록된 차량 수
- **online**: 최근 heartbeat/응답이 확인된 차량 수
  - 현재 단계에서는 파일 기반 신호(예: `fleet-ack`)로 `deploy/state/<car>.json`의 `last_seen_at`을 갱신해 last-seen 기반 online 판단을 준비합니다.

자세한 필드/판단 기준은 `deploy/README_VEHICLE_ONLINE_STATUS.md` 참고.

