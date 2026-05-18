# Backend Contract Decision

## 1. Purpose

Phase 4에서 실제 API 연결 전, Control Plane 배포 명령이 어떤 backend를 기준으로 동작할지 결정한다.

이 문서는 Phase 3까지 작성된 read-only/dry-run adapter 결과를 바탕으로, Control Plane 명령의 표준 진입점과 내부 backend 역할을 고정하기 위한 결정 문서다. 실제 backend 연결, 상태 변경, CLI/script/API 호출은 이 단계에서 수행하지 않는다.

## 2. Recommended Backend Boundary

추천 구조:

- Control Plane 표준 진입점: `status-server` API
- 내부 실행 backend: `DeploymentMgr` CLI, `ota_vehicle.sh`, `Signal_I_F`
- Dashboard는 직접 CLI/script를 호출하지 않고 `status-server` API만 호출한다.

즉, Dashboard는 Control Plane UI이고, `status-server`는 Control Plane API gateway다. `DeploymentMgr`, `ota_vehicle.sh`, `Signal_I_F`는 각각 상태 관리, runtime demo, control signal 발행을 담당하는 내부 backend로 둔다.

## 3. Backend Roles

### status-server API

역할:

- Control Plane API gateway
- Dashboard 요청 수신
- 배포 명령 검증
- `DeploymentMgr` CLI 또는 script 호출
- 결과를 UI에 반환

`status-server`는 Phase 4에서도 Dashboard의 표준 API 진입점으로 유지한다. Dashboard가 직접 `DeploymentMgr`나 shell script를 호출하지 않도록 한다.

### DeploymentMgr CLI

역할:

- fleet deploy 상태 관리
- `version_map.json` / `package_registry.json` / `deploy/state` 처리
- rollback / shadow / kill 상태 관리

`DeploymentMgr`는 fleet deployment state backend로 둔다. 즉, 차량별 배포 상태, rollback metadata, fleet history 등 file-based deployment state 변경의 표준 backend다.

### ota_vehicle.sh

역할:

- Docker 기반 vehicle runtime demo
- Primary/Shadow 컨테이너 실행 시연
- 실제 배포 상태 관리의 표준 backend가 아니라 runtime demo backend

`ota_vehicle.sh`는 Docker runtime demonstration을 위한 backend로 유지한다. 표준 fleet state는 `DeploymentMgr`가 관리하고, Docker container 상태 시연은 `ota_vehicle.sh`가 담당한다.

### Signal_I_F

역할:

- Kill Switch / Shadow / Deploy 제어 신호 전송
- MQTT 기반 제어 메시지 발행
- Vehicle Plane `SignalReceiver` / `SignalSender`와 연결되는 Signal Interface

`Signal_I_F`는 control signal interface로 둔다. `status-server` 또는 Control Plane signal service가 호출할 수 있지만, 실제 MQTT message 발행 가능성이 있으므로 Phase 4 전까지 자동 연결하지 않는다.

## 4. Command Mapping Decision

| Primary/Candidate Action | Standard Control Plane API | Legacy Backend | Runtime Demo Backend | Current Status | Notes |
|---|---|---|---|---|---|
| `deploy-candidate` | `POST /api/fleet/deploy` 또는 향후 command preview API | `DeploymentMgr fleet-deploy ...` | 없음 | Existing backend available, adapter dry-run only | Control Plane은 Candidate Version 배포 의도를 API로 받고, legacy 내부에서는 candidate slot 호환 표현으로 변환한다. |
| `run-shadow` | `POST /api/fleet/action` 또는 향후 command preview API | `DeploymentMgr fleet-shadow vehicle ... ON` | `ota_vehicle.sh deploy-shadow ...` | Existing backend available, adapter dry-run only | State-level Shadow는 `DeploymentMgr`, Docker runtime Shadow 시연은 `ota_vehicle.sh`가 담당한다. |
| `promote-candidate` | `POST /api/fleet/action` 또는 향후 command preview API | `DeploymentMgr fleet-promote-shadow vehicle ...` | `ota_vehicle.sh promote ...` | Existing backend available, adapter dry-run only | State metadata promotion과 Docker runtime promotion은 구분한다. |
| `rollback-primary` | `POST /api/fleet/action` 또는 향후 command preview API | `DeploymentMgr fleet-rollback vehicle ...` | 필요 시 별도 runtime rollback 절차 | Existing backend available, adapter dry-run only | 표준 rollback state backend는 `DeploymentMgr`로 둔다. |
| `activate-kill-switch` | `POST /api/fleet/action` 또는 Signal I/O API | `DeploymentMgr fleet-kill vehicle ...` 또는 `Signal_I_F POST /api/v1/control/kill-switch` | 없음 | Existing backends available, adapter dry-run only | File state kill과 MQTT control signal kill은 역할이 다르므로 Phase 4에서 policy를 확정한다. |
| `clear-kill-switch` | TODO: endpoint 확정 필요 | TODO: `DeploymentMgr` clear command 여부 확인 필요 | 없음 | Not confirmed | 현재 명확한 clear endpoint/CLI가 확정되지 않았으므로 TODO로 유지한다. |

## 5. Reason Handling Policy

- API에서는 `reason` 원문 유지 가능.
- CLI에서는 공백/특수문자를 안전한 token으로 normalize.
- Adapter가 backend별 reason formatting을 담당한다.

정책 제안:

1. Control Plane API request에는 human-readable reason을 보존한다.
2. `DeploymentMgr fleet-kill`처럼 legacy CLI가 single token을 기대하는 경우 adapter가 safe token을 별도로 생성한다.
3. UI와 history에는 원문 reason과 normalized reason을 함께 보관할 수 있도록 Phase 4 schema를 설계한다.
4. Signal I/F API가 원문 reason을 지원하지 않는 경우, message detail 확장은 별도 protocol version으로 검토한다.

## 6. Why We Do Not Connect Yet

아직 실제 backend에 연결하지 않는 이유:

- 기존 `status-server/server.js`는 여러 책임이 결합되어 있음.
- `DeploymentMgr`와 `ota_vehicle.sh`는 상태를 실제 변경할 수 있음.
- `Signal_I_F`는 실제 MQTT 제어 메시지를 발행할 수 있음.
- 따라서 먼저 dry-run adapter로 command mapping을 검증한다.

Phase 3-D의 `DeploymentCommandAdapter`는 JSON preview만 생성하고, 실제 `DeploymentMgr`, `ota_vehicle.sh`, `Signal_I_F`를 호출하지 않는다. Phase 4에서도 우선 preview API 또는 service boundary를 검증한 뒤 실제 연결을 결정해야 한다.

## 7. Phase 4 Implementation Plan

### Phase 4-A

- `status-server` 수정 없이 adapter 출력 결과를 문서로 검증.
- 명령별 expected preview JSON을 확정.

### Phase 4-B

- `status-server` 내부 route/service boundary 설계.
- 기존 `server.js`를 즉시 분리하지 않고, route group과 service 책임만 문서화.

### Phase 4-C

- dry-run command preview API를 `status-server`에 추가할지 결정.
- 추가한다면 실제 backend 실행 없이 preview JSON만 반환하도록 제한.

### Phase 4-D

- Dashboard에는 아직 연결하지 않고 API smoke test 진행.
- curl/Postman/CLI로 preview API만 검증.

### Phase 4-E

- 실제 backend 실행 연결.
- `DeploymentMgr`, `ota_vehicle.sh`, `Signal_I_F` 중 어떤 backend를 언제 호출할지 action별 feature flag 또는 config로 제어.

## 8. Risks

- 기존 route와 response shape 변경 시 Dashboard 깨질 위험.
- `DeploymentMgr` CLI command contract 변경 시 `status-server` 깨질 위험.
- `ota_vehicle.sh` 실행 시 Docker runtime 상태 변경 위험.
- `Signal_I_F` 호출 시 실제 MQTT message 발행 위험.
- Kill Switch clear semantics가 확정되지 않은 상태에서 endpoint를 만들면 안전 정책 혼선 위험.
- State-level Shadow와 Docker runtime Shadow를 혼동하면 demo 결과와 file-based state가 불일치할 위험.

## 9. Final Decision Summary

- Control Plane의 표준 진입점은 `status-server` API로 둔다.
- `DeploymentMgr` CLI는 fleet deployment state backend로 둔다.
- `ota_vehicle.sh`는 Docker runtime demo backend로 둔다.
- `Signal_I_F`는 control signal interface로 둔다.
- Phase 4 전까지 실제 backend 호출은 하지 않는다.
