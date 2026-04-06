# SDV Feature Flag Platform — 팀 공통 규약

플랫폼 **실행·설정·EventBus·Feature 베이스**와 **Kill Switch / Shadow Mode / A/B Mode**까지 한 문서에서 규정한다.  
구현·리뷰 시 이 문서와 코드가 어긋나면 **문서 또는 코드 중 하나를 PR로 맞춘다**.

---

## 1. 실행 모델

- **프로세스 격리**: 각 Feature·daemon은 독립 프로세스로 실행한다.
- **오케스트레이터**: `SdvPlatform` 하나 (`LifeCycleMgr`). 설정 로드 → 기동 → 모니터 → 종료.
- **프로세스 종류**
  - **communication**: `EventBusHost`, `DataCollector`, `SignalReceiver`, `SignalSender` 및 동일 역할의 실행 파일.
  - **feature**: `FeatureEvaluator`, `WatchDog`, 도메인 Feature 및 **Shadow 전용·A/B 변형** 실행 파일(9·10절).
- **자식 프로세스 인자**: SdvPlatform은 spawn 시 **`argv[1]`에 단일 문자열 `host:port`** 만 전달한다. 모든 자식은 `argc > 1`이면 `argv[1]`을 EventBus 접속 주소로 사용한다.
- **기동 순서**: 설정 파일 순서대로 **communication 전부**를 먼저 spawn(항목 사이 약 1초) → **feature 전부**(항목 사이 약 500ms).
- **규칙**: **`EventBusHost`는 `communication` 목록에서 항상 가장 먼저** 둔다.

---

## 2. 설정 파일 (`config/SdvPlatform.conf`)

- **해석 키워드 3개**: `address`, `communication`, `feature`. 빈 줄과 `#` 시작 줄은 무시.
- **`address`**: `host:port` 한 토큰 (예: `0.0.0.0:50051`).
- **등록**: `communication <실행파일이름>`, `feature <실행파일이름>`. 값은 **빌드 산출 실행 파일 이름과 정확히 일치**해야 한다.
- **Shadow**: 섀도 전용 프로세스는 별도 실행 파일로 등록한다 (9절 명명 규칙).
- **A/B**: 변형 A/B를 **서로 다른 실행 파일**로 두는 방식을 표준으로 한다 (10절). (`argv` 확장 없이 LifeCycle과 호환.)
- **SdvPlatform 기본 설정 경로**: `config/SdvPlatform.conf` (`argv[1]`로 변경 가능).

---

## 3. EventBus (IPC)

- **구현**: gRPC. 서버 `EventBusHost`, 클라이언트 `EventBusProxy` / `EventBusInterface`.
- **등록**: `registerComponent()` → 종료 전 `unRegister(componentId)`.
- **수신**: `getEventQueue(componentId)`는 **복사 후 원본 큐 비움** → poll 1회 = consume.
- **지연**: 제어 이벤트는 Feature 쪽 **poll 주기**에 묶인다. Kill Switch도 **즉시 SIGKILL이 아니라 협력적 중단**이 기본이다 (8절).

---

## 4. EventId (`EventId.h`) — 팀 확정 값

아래 **숫자·이름·의미**를 코드 enum과 동일하게 유지한다. 새 ID가 필요하면 **20–29 실험 구간**을 쓰고, 이 표와 `EventId.h`를 동시에 갱신한다.

| 구분 | 이름 | 값 | message 규약 |
|------|------|-----|----------------|
| 데이터 | `VehicleLocationUpdate` | 1 | 자유 문자열 (단일 줄 권장) |
| 데이터 | `RoadInfoUpdate` | 2 | 자유 문자열 (단일 줄 권장) |
| 데이터 | `NavigationGuidanceUpdate` | 3 | 자유 문자열 (단일 줄 권장) |
| 데이터 | `AdasDecisionUpdate` | 4 | 자유 문자열 (단일 줄 권장) |
| 데이터 | `EnergyStateUpdate` | 5 | 자유 문자열 (단일 줄 권장) |
| 플래그 | `FeatureFlagOn` | 10 | 대상 Feature **논리 이름** |
| 플래그 | `FeatureFlagOff` | 11 | 대상 Feature **논리 이름** |
| 안전 | `KillSwitch` | 12 | 대상 Feature **논리 이름** |
| 섀도 데이터 | `ShadowMirrorData` | 20 | 세그먼트 3개: `MIRROR` + (ASCII 세로막대) + 원본 EventId(10진) + (세로막대) + payload. **payload 안에 세로막대 금지** (9.2 예시 참고) |
| 섀도 제어 | `ShadowModeOn` | 21 | **Primary** Feature 논리 이름 |
| 섀도 제어 | `ShadowModeOff` | 22 | **Primary** Feature 논리 이름 |
| A/B | `AbVariantAssign` | 23 | Feature 논리 이름 + (ASCII 세로막대) + `A` 또는 `B` — 예: `NavigationFeature` 세로막대 `A` |
| 시스템 | `QuitTheSystem` | 99 | 자유 (관례: 이유 한 줄) |

**구현 순서 권장**: 10–12 (기존) → 21–22 (섀도 스위치) → 20 (미러 전송) → 23 (A/B) → WatchDog·FeatureEvaluator·`Feature::pollEvents()` 반영.

---

## 5. Feature 공통 베이스 (`Feature` / `Feature.cpp`)

- 구현체는 `Feature`를 상속하고 **`execute()`만 구현**한다.
- **생명주기**: (기존과 동일) Flag 확인 → `registerComponent()` → `execute()` → `unRegister()`.
- **`pollEvents()` 선처리 (팀 목표 동작)**  
  - **반드시 유지**: `FeatureFlagOn` / `FeatureFlagOff` / `KillSwitch` — message가 `name_`과 일치할 때만 enable / disable / `requestStop()`.  
  - **추가 구현**: `ShadowModeOn` / `ShadowModeOff` — message가 **이 프로세스의 Primary 이름과 일치**할 때만 내부 플래그 `shadowMirrorEnabled_`(이름 예시)를 true/false. Primary만 true일 때 도메인 이벤트를 나간 뒤 `ShadowMirrorData`를 송신한다 (9절).  
  - **추가 구현 (단일 바이너리 A/B 선택 시)**: `AbVariantAssign` — message가 `name_|\*` 형태이면 파싱해 로컬 variant 저장. **두 실행 파일 A/B 모델**이면 이 이벤트는 선택 사항(10절).
- **시그널**: `SIGINT` / `SIGTERM` → `Feature::signalHandler`.
- **이름 일치**: `Feature("이름", bus)` = config `feature` 줄 = 플랫폼 제어 message 대상 이름.
- **static `stopRequested_`**: 프로세스당 Feature 인스턴스 **하나**.

---

## 6. 메시지 문자열·구분자

- **공통**: Event message는 **가능하면 한 줄**. 파이프 `|`는 이 문서에서 정한 **필드 구분자**로 쓰인다. 값 필드에 `|`가 필요하면 **문서·코드 동시에 개정** (예: 길이 프리픽스 또는 JSON 전환).
- **플랫폼 제어 (10–12)**: message = Feature 논리 이름만 (추가 접미사 없음).
- **FeatureEvaluator 인바운드 파일** (한 줄 한 명령):

| ACTION | 형식 | 전송 Event / 비고 |
|--------|------|-------------------|
| `FLAG_ON` | `FLAG_ON:featureName` | `FeatureFlagOn` |
| `FLAG_OFF` | `FLAG_OFF:featureName` | `FeatureFlagOff` |
| `KILL` | `KILL:featureName` | `KillSwitch` + 기존 WARNING 파일 출력 |
| `DMS_SLEEP` | `DMS_SLEEP:featureName:payload` | `KillSwitch`로 매핑 (DMS: 운전자 잠듬, 데모 표준) |
| `ADAS_FATAL` | `ADAS_FATAL:featureName:payload` | `KillSwitch`로 매핑 (ADAS 치명적 오류, 데모 표준) |
| `SENSOR_FATAL` | `SENSOR_FATAL:featureName:payload` | `KillSwitch`로 매핑 (센서/입력 치명적 오류, 데모 표준) |
| `MANUAL_KILL` | `MANUAL_KILL:featureName:payload` | `KillSwitch`로 매핑 (사람 강제 중단, 데모 표준) |
| `DRIVER_SLEEP` | `DRIVER_SLEEP:featureName:payload` | `DMS_SLEEP` 별칭(alias) — `KillSwitch`로 매핑 |
| `DMS_DRIVER_SLEEP` | `DMS_DRIVER_SLEEP:featureName:payload` | `DMS_SLEEP` 별칭(alias) — `KillSwitch`로 매핑 |
| `DRIVER_DROWSY` | `DRIVER_DROWSY:featureName:payload` | `DMS_SLEEP` 별칭(alias) — `KillSwitch`로 매핑 |
| `SHADOW_ON` | `SHADOW_ON:PrimaryName` | `ShadowModeOn` |
| `SHADOW_OFF` | `SHADOW_OFF:PrimaryName` | `ShadowModeOff` |
| `AB_SET` | `AB_SET:FeatureName:A` 또는 `AB_SET:FeatureName:B` | `AbVariantAssign` message `FeatureName|A` 등 |

`ACTION:featureName:extra`에서 `extra`는 위 표에 쓰인 경우에만 해석한다.

---

## 7. 운영·리뷰

- **작업 디렉터리**: SdvPlatform과 동일 cwd (`logs/` 등).
- **재기동**: communication만 자동 재기동; feature는 없음.
- **로그**: `logs/<이름 공백 제거>.log` — 논리 이름에 공백 금지.

### PR 체크리스트

- [ ] config 이름 = 실행 파일명 = `Feature("…")` 이름?
- [ ] `EventId.h` ↔ 이 문서 4절 표 일치?
- [ ] Kill/Shadow/A-B 동작이 8–10절과 일치?
- [ ] `EventBusHost`가 communication 목록 최상단?
- [ ] gRPC·공유 헤더 변경 시 리뷰 규칙 충족?

---

## 8. Kill Switch — 공통 규약

**목적**: 지정 Feature를 **위험 차단 목적**으로 빠르게 멈추게 한다. (기능 끄기와 구분: 8.4)

### 8.1 트리거

- EventBus `KillSwitch` (12), message = 대상 Feature 논리 이름.
- FeatureEvaluator 인바운드: `KILL:FeatureName`.
- 다른 컴포넌트가 직접 `sendEvent` 하는 것은 **비상·내부 도구**로만 허용하고, 운영 경로는 Evaluator로 통일한다.

### 8.2 수신 측 동작 (대상 Feature)

- `Feature::pollEvents()`에서 처리: 이름 일치 시 **`requestStop()`** 만 호출 (현행 코드와 동일).
- **`execute()`** 루프는 **`isStopRequested()`** 를 주기적으로 검사해 **짧은 시간 안에 루프 탈출**한다.
- Kill Switch는 **Flag를 자동으로 OFF로 바꾸지 않는다** (의미 분리). 필요 시 운영에서 `FLAG_OFF`를 **별도로** 보낸다.

### 8.3 FeatureEvaluator 부가 동작

- 기존처럼 outbound에 `WARNING:target:KillSwitch activated` 한 줄 append (SignalSender 연계 관례 유지).

### 8.4 `FeatureFlagOff`와의 차이

| | `FeatureFlagOff` | `KillSwitch` |
|--|------------------|--------------|
| 목적 | 기능 비활성(출력/의사결정 중단 등) | 비상 정지 |
| 기본 효과 | Flag 비활성 | stop 요청 |
| 다음 기동 | Flag가 꺼져 있으면 `run()` 초기에 미기동 | 이전과 무관, 프로세스는 재기동 시 다시 뜰 수 있음 |

### 8.5 WatchDog / 관측

- `KillSwitch` 이벤트는 로그·메트릭 대상으로 남긴다 (기존 `logFlagEvent` 흐름 유지·확장).

### 8.6 실시간성 한계 (고정)

- EventBus + poll 구조상 **수백 ms ~ 수 초** 지연이 날 수 있다. **하드 리얼타임 안전망**은 차량 ECU·별도 채널 책임이다.

---

## 9. Shadow Mode — 공통 규약

**목적**: Primary가 내는 **같은 입력 흐름**을 Shadow가 받아 **시뮬레이션·로깅·비교**만 하고, **차량/외부 시스템에 영향을 주는 이벤트는 Primary만** 보낸다.

### 9.1 프로세스 모델

- **Primary**: 기존 도메인 Feature (예: `NavigationFeature`).
- **Shadow**: 별도 실행 파일 **`<Primary이름>Shadow`** (예: `NavigationFeatureShadow`). config에 `feature`로 **둘 다** 등록.
- Shadow 프로세스의 `Feature("…")` 이름은 **실행 파일명과 동일** (`NavigationFeatureShadow`).

### 9.2 데이터 경로

- Primary는 `ShadowModeOn`을 받은 뒤에만(내부 `shadowMirrorEnabled_ == true`) 다음을 수행한다:  
  도메인 이벤트를 bus로 보낼 때마다 **추가로** `ShadowMirrorData` (20)를 한 번 더 보낸다.
- **message 형식**: `MIRROR|<원본_EventId_숫자>|<payload>`  
  - 예: 원본이 `VehicleLocationUpdate`이고 payload가 `서울…`이면  
    `MIRROR|1|서울…`
- **멀티 Shadow 필터링(현행 구현)**: payload 앞에 `<PrimaryName>:` 접두사를 둘 수 있다.  
  예: `MIRROR|1|NavigationFeature:서울…`  
  (`|` 세그먼트는 유지하면서, Shadow 프로세스가 자기 Primary 것만 consume하도록 하기 위함)
- **Shadow Feature**는 `ShadowMirrorData`만 파싱해 자기 로직에 넣고, **다시 `VehicleLocationUpdate` / `RoadInfoUpdate` 등을 bus에 쏘지 않는다** (위반 시 코드 리뷰에서 반려).

### 9.3 제어

- `ShadowModeOn` (21) / `ShadowModeOff` (22): message = **Primary** 논리 이름. Primary만 consume해서 mirroring on/off.
- Shadow 프로세스 자체의 켜짐/끔은 기존 `FeatureFlagOn`/`Off`로 **Shadow 실행 파일 이름**을 대상으로 한다.
- 운영 입력: `SHADOW_ON:PrimaryName`, `SHADOW_OFF:PrimaryName` (6절).

### 9.4 실패·성능

- Mirror 송신은 Primary의 **베스트 에포트**. 실패 시 warn 로그만 남기고 Primary 본 업무는 계속할지 여부는 팀이 Feature별로 정하되, **기본은 Primary 송신 성공을 우선**한다.

---

## 10. A/B Mode — 공통 규약

**목적**: 동일 역할에 대해 **A안/B안** 중 하나만 대외적으로 “승인된 경로”로 동작하게 한다.

### 10.1 프로세스 모델 (표준)

- **두 실행 파일**: `<BaseName>A`, `<BaseName>B` (예: `NavigationFeatureA`, `NavigationFeatureB`).
- config에는 **실험 정책에 따라 하나만** `feature`로 넣거나, 둘 다 넣되 **한쪽만 `FeatureFlagOn`** 인 상태로 운영하는 방식을 택한다. **동시에 둘 다 외부 영향 이벤트를 쏘면 안 된다.**

### 10.2 단일 프로세스 + variant (선택)

- 하나의 `NavigationFeature`만 띄우고 `AbVariantAssign`으로 내부 분기만 바꿀 경우:  
  message `NavigationFeature|A` 또는 `NavigationFeature|B`.  
  `execute()`는 variant에 따라 코드 경로만 갈라진다. **대외 이벤트는 variant 하나만 활성**이어야 한다.

### 10.3 제어

- EventBus: `AbVariantAssign` (23), message `FeatureName|A` 또는 `FeatureName|B`.
- FeatureEvaluator: `AB_SET:FeatureName:A` / `AB_SET:FeatureName:B` (6절).
- **이름 일치**: `FeatureName`은 `Feature("…")` / config와 동일.

### 10.4 Shadow·A/B 동시 사용

- Shadow는 **선택된 variant(또는 활성 Primary 한 개)** 기준으로만 mirror 하도록 한다.  
- A/B **둘 다** 살아 있을 때는 mirror 대상을 문서화된 **하나의 Primary 이름**으로만 둔다 (운영 혼선 방지).

---

## 문서 이력

| 날짜 | 내용 |
|------|------|
| 2026-04-01 | 초안 — 코드 기준 규약 정리 |
| 2026-04-01 | Kill Switch / Shadow Mode / A/B Mode 규약 확정 (EventId 20–23, 메시지·Evaluator·프로세스 모델) |
