## 목적

이 문서는 현재 구현되어 있는 fleet 배포 기능(`fleet-deploy` + `vehicles.json`) 위에서,
**자동 스케줄러/잡큐 없이도** canary → beta → stable 순서로 “점진적 배포(Progressive Rollout)”를 운영하는 방법과
추후 자동화를 위한 메타데이터 필드를 정리합니다.

중요: 이번 단계는 **문서/메타데이터 정리만** 수행하며,
DeploymentManager의 배포 로직을 변경하거나 UI/서버를 수정하지 않습니다.

---

## 현재 Fleet 타겟팅 구조(이미 구현됨)

- Fleet registry: `config/fleet/vehicles.json`
  - `car_id`, `model`, `region`, `group`를 관리
  - 데모 기준 group 예: `canary`, `beta`, `stable`
- DeploymentManager CLI:
  - `fleet-deploy vehicle <car_id> <FEATURE> <SLOT> <VERSION>`
  - `fleet-deploy group <group> <FEATURE> <SLOT> <VERSION>`
  - `fleet-deploy region <region> <FEATURE> <SLOT> <VERSION>`
  - `fleet-deploy group_region <group> <region> <FEATURE> <SLOT> <VERSION>`
- History:
  - 모든 fleet 명령은 `deploy/history/fleet_history.jsonl`에 JSONL 라인으로 기록됨(요약 + 결과)

---

## Progressive Rollout 권장 운영 흐름 (canary → beta → stable)

배포 단위는 Feature별 Docker 이미지(예: `sdv/navigation-feature:v2.0`)이며,
Shadow는 “Candidate 검증 단계”입니다(Primary는 Promote 전까지 변경되지 않음).

### 단계 0: 준비(패키지/이미지 식별)

- manifest 기준으로 후보를 명확히 식별
  - `domain`, `feature`, `version`, `docker_image`, `image_digest` (가능 시)
  - 선택적으로 `rollout_*` 필드로 캠페인 정보 기록(아래 참고)

### 단계 1: canary 그룹에 Shadow 배포(Primary 유지)

- 목표: canary 차량들에 Candidate를 Shadow로 올려 관측/검증 시작
- 예(개념):
  - 런타임(도커 데모) 기준: `scripts/ota_vehicle.sh deploy-shadow ...`
  - fleet(state) 기준: `fleet-shadow vehicle ...`로 Shadow 상태 기록(Primary는 변경되지 않음)

### 단계 2: canary Shadow 검증

- 목표: canary에서 Shadow의 검증 결과(로그/메트릭/ACK 등)를 수집
- 실패:
  - Primary는 유지
  - 실패 후보는 `blocked_versions` 또는 `quarantine`으로 분리(재시도/승격 대상에서 제외)

### 단계 3: canary에서 Promote(Shadow → Primary)

- 목표: canary에서 검증된 후보를 Primary로 승격
- 성공 시:
  - `rollback_stack`에 이전 안정 Primary를 적재(최대 3개)
- 실패(승격 후 Primary 문제):
  - 진짜 rollback(Primary runtime failure)로 취급
  - 현재 Primary는 blocked/quarantine 대상으로 분리

### 단계 4: beta 그룹으로 확대

- canary와 동일한 Shadow→검증→Promote 단계를 beta에 적용

### 단계 5: stable 그룹으로 확대

- 최종적으로 stable에 적용(전면 배포)

---

## 메타데이터(선택): rollout_* 필드 (추후 자동화 대비)

현 단계에서는 DeploymentManager가 이 필드를 사용하지 않습니다.
하지만 manifest/package registry가 “캠페인/점진 배포 계획”을 표현할 수 있도록 optional로 정의합니다.

- `rollout_stage`: 문자열(예: `"canary" | "beta" | "stable"`)
- `rollout_group`: 문자열(예: `"canary"`, `"beta"`, `"stable"` 또는 custom group)
- `rollout_order`: 숫자(예: canary=1, beta=2, stable=3)
- `rollout_policy`: 객체(예: min_success_rate, max_failure_count 등 — 정책 문서용)
- `promotion_required`: boolean (예: true면 canary 검증 후 반드시 promote 승인 필요)

실제 자동화를 붙이는 단계에서는:
- `fleet_history.jsonl`을 “캠페인 이벤트 로그”로 사용하거나,
- 별도 `deploy/rollout/` 상태 파일(큐/락/승인)로 확장하는 접근을 고려할 수 있습니다.

