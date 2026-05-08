## 목적

이 문서는 fleet 관점에서 **registered vehicles**와 **online vehicles**를 명확히 구분하기 위한 기준과,
이를 state 파일(`deploy/state/<car>.json`)에 표현하는 최소 필드를 정의합니다.

이번 단계(15단계)에서는 MQTT/HTTP heartbeat 통신을 구현하지 않고,
파일 기반/CLI 기반 신호(ACK, 프로세스 probe 등)로 “last seen” 개념을 준비합니다.

---

## 정의

- **registered vehicles**
  - 의미: fleet registry에 등록된 전체 차량
  - 소스: `config/fleet/vehicles.json`의 entries 개수

- **online vehicles**
  - 의미: 최근 일정 시간 이내에 heartbeat/상태 응답이 확인된 차량
  - 소스(현재 단계): `deploy/state/<car>.json`의 `last_seen_at` 또는 Status Server의 runtime probe

- **offline vehicles**
  - 의미: registered이지만 일정 시간 이상 last_seen이 갱신되지 않은 차량

---

## state 필드(선택) — `deploy/state/<car>.json`

아래는 기존 포맷을 깨지 않기 위해 **optional** 로 추가되는 top-level 필드입니다.

- `last_seen_at` (string, UTC ISO-8601)
  - 마지막으로 “차량 응답/관측”이 확인된 시각
- `heartbeat_source` (string)
  - last_seen_at을 갱신한 출처
  - 예: `ack` | `wsl_pgrep` | `mqtt` | `http` | `unknown`
- `online_status` (string)
  - `online` | `offline` | `unknown`
  - 권장: 서버/대시보드에서 계산하고, state에는 굳이 고정 저장하지 않아도 됨(캐시/표시용이면 OK)

---

## online 판단 기준(권장)

아래 기준은 “표시/운영” 기준이며, 이번 단계에서는 구현을 강제하지 않습니다.

- `now - last_seen_at <= 2분` 이면 online
- `last_seen_at`이 없거나 너무 오래되면 offline

참고:
- 현재 Dashboard는 Status Server가 제공하는 `runtime.car_process_running`(WSL pgrep) 값을 우선시할 수 있습니다.
- 추후 MQTT/HTTP heartbeat가 도입되면 `heartbeat_source=mqtt|http`로 전환합니다.

---

## ACK와 last_seen_at (현재 단계 연결)

15단계에서는 `DeploymentMgr fleet-ack ...`가 파일 기반 “차량 응답” 역할을 하므로,
ACK 기록 시 `deploy/state/<car>.json`의 `last_seen_at`을 함께 갱신하는 방식을 권장합니다.

