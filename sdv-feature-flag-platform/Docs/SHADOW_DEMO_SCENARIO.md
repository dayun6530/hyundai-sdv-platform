# Shadow Mode(그림자 실행) 데모 시나리오 — 확정본

**전제:** 신규 버전을 **병렬**로 돌리되 **차량·HMI 제어는 Primary만** 담당하고, Shadow는 **로그·데이터(미러)만** 수집해 검증한다. **코드 구현을 늘리기 전에 이 시나리오와 수집 항목·승인 절차를 팀이 먼저 고정한다.**

Primary는 `sendDomainEvent`로 도메인 이벤트를 보내고, Shadow Mode ON일 때만 동일 내용이 **`ShadowMirrorData`(20)** 로 추가 송신된다. Shadow 프로세스는 미러를 소비해 기록·분석만 하며, **도메인 이벤트를 bus로 재송신하지 않는다** (`Feature::sendDomainEvent` 가드). 관제 경로는 `SignalReceiver` → `logs/SignalsInbound.cmd` → `FeatureEvaluator` → `ShadowModeOn`(21) / `ShadowModeOff`(22). 규약은 `Docs/CONVENTIONS.md` 6·9절.

---

## 수용 기준 (반드시 문서·데모에서 명시)

| 기준 | 내용 |
|------|------|
| **사용자 화면·체감 동작 불변** | Shadow ON/OFF와 무관하게 **운전자/사용자에게 보이는 UI·음성·조향/가속/제동 등 차량 제어 출력은 변하지 않아야 한다.** 변화는 **Shadow 측 로그·메트릭·내부 EventBus 관측**에만 나타난다. |
| **제어 주체** | 실차에 영향을 주는 명령·상태 발행은 **Primary만** 수행한다. |
| **ON 전환** | **`SHADOW_ON` / 관제 반영은 사전 승인(아래 게이트) 이후에만** 수행한다. |

---

## 동시 수집 로그·메트릭 항목 (프로토타입 기준)

데모·시험 시 **동일 시간대에** 아래를 수집·대조한다. (팀 합의로 추가/삭제해 확정한다.)

| 구분 | 항목 | 출처 / 확인 포인트 |
|------|------|---------------------|
| 제어 | `ShadowModeOn`(21), `ShadowModeOff`(22) 수신 | EventBusHost 콘솔 `Event Information(ID: 21|22, Message: <Primary>)`, `logs/FeatureEvaluator.log` (`SHADOW_ON` / `SHADOW_OFF` → Primary) |
| 외부 입력 | 관제·센터 시그널 반영 | `[SignalReceiver] Received: …`, (필요 시) `logs/SignalsInbound.cmd` 흐름 |
| Primary | 미러링 활성 여부 | `logs/<Primary>.log` — `ShadowMode ON/OFF (mirroring …)` |
| Shadow | 미러 수신·유형 | `logs/<Primary>Shadow.log` — `mirror received`, 원본 EventId별 로그 (`VehicleLocationUpdate`, `RoadInfoUpdate`, `NavigationGuidanceUpdate`, `AdasDecisionUpdate`, `EnergyStateUpdate` 등) |
| 안전 | Shadow의 도메인 송신 차단 | (의도적 테스트 시) Shadow에서 도메인 송신 시도 시 차단 로그 |
| 프로세스 | 생존 상태 | `[LifeCycleMgr]` 상태표 — Shadow Mode에서도 **Primary·Shadow 프로세스는 통상 `running` 유지** (KillSwitch와 구분) |

**메트릭(확장 시):** 미러 이벤트 수/초, Primary 대비 Shadow 처리 지연, Drop 건수(EventBus queue full 등). 팀에서 대시보드·수집 도구 정의 후 이 표에 행 추가.

---

## 승인 게이트 (ON 전환 전)

1. 이 문서의 **수용 기준·수집 항목**이 현재 릴리스/데모 범위에 맞게 채워졌는지 검토한다.  
2. **담당(예: 플랫폼/안전/기능 오너)** 가 `SHADOW_ON` 적용 범위(어떤 Primary, 어떤 환경, 얼마 동안)에 **서면 또는 이슈 승인**을 남긴다.  
3. 승인 전에는 `config/ExternalSignals.cmd` 등에 **운영 반영 금지**(데모는 격리 환경에서만).  
4. 승인 후에만 `SHADOW_ON:<Primary>` 주입 또는 관제 반영을 수행한다.

---

## 팀 합의 후 시나리오 확정

- **확정 산출물:** 수집 항목 표, 승인 게이트 담당, 데모/시험 환경(로컬·HIL 등), Shadow ON 허용 Primary 목록, 종료 조건(`SHADOW_OFF` 또는 플랫폼 종료).  
- **합의 기록:** 회의록·백로그 코멘트·PR 설명 중 한 곳에 “Shadow 시나리오 확정” 링크를 남긴다.  
- 확정 이후 변경은 **소규모 CR/리뷰**로 반영한다.

---

## 백로그 행 — 스프레드시트에 그대로 붙여 넣기 (TSV)

아래 **한 줄**을 복사해 붙이면 열이 나뉩니다. (탭으로 구분됨)

```text
6	Feature Management	Shadow Mode(그림자 실행) 데모 시나리오 확정	신규 버전을 병렬 실행하되 차량 제어는 하지 않고 로그/데이터만 수집해 검증한다. 구현보다 시나리오 고정이 먼저다. (`Docs/SHADOW_DEMO_SCENARIO.md` — 사용자 체감 불변, 수집 항목, 승인 후 ON, 팀 합의 확정.)	"Shadow Mode 데모 시나리오 문서(`Docs/SHADOW_DEMO_SCENARIO.md`)를 읽고 수용 기준을 확인한다.
사용자 화면·체감 동작에 변화가 없어야 한다는 기준을 문서·백로그에 명시한다(Shadow는 로그/미러 관측만).
동시에 수집할 로그/메트릭 항목을 문서 표로 정의하고 팀과 맞춘다.
담당 승인 후에만 `SHADOW_ON`(또는 관제 동등 명령)으로 전환하는 단계를 거친다.
팀 합의 후 시나리오를 확정하고(회의록/이슈 링크), 확정본만 데모·시험에 사용한다."
```

### 스프레드시트 수동 입력용

| 항목 | 내용 |
|------|------|
| ID | 6 |
| 영역 | Feature Management |
| 제목 | Shadow Mode(그림자 실행) 데모 시나리오 확정 |
| 설명 | 신규 버전을 병렬 실행하되 차량 제어는 하지 않고 로그/데이터만 수집해 검증한다. 구현보다 시나리오 고정이 먼저다. |
| 검증 절차 | 위 TSV 5번째 열 인용 블록과 동일 |

---

## 부록: 프로토타입 실행 검증 (시나리오 확정·승인 후)

승인된 데모에서만 아래를 수행한다.

1. 프로젝트 루트에서 SdvPlatform 실행. EventBusHost, SignalReceiver, SignalSender, FeatureEvaluator, 각 Primary·Shadow가 **running**인지 `[LifeCycleMgr] --- Process Status ---`로 확인.

```bash
cd /mnt/c/Users/82107/Desktop/팀플/sdv-feature-flag-platform && ./build/SdvPlatform
```

2. (주입 예) 승인된 Primary에 대해:

```bash
printf 'SHADOW_ON:NavigationFeature\nSHADOW_ON:AdasFeature\nSHADOW_ON:EvEnergyFeature\n' >> config/ExternalSignals.cmd
```

3. **성공 확인 (수집 항목 표와 대조):**  
   `[SignalReceiver] Received: …`, `Event Information(ID: 21, …)`, Primary 로그의 mirroring ON, 각 `*Shadow.log`의 mirror 수신, `FeatureEvaluator.log`, Primary·Shadow **running** 유지.

**Shadow OFF (승인된 종료 시):**

```bash
printf 'SHADOW_OFF:NavigationFeature\n' >> config/ExternalSignals.cmd
```

---

## 참고

- `FeatureEvaluator`는 시작 후 약 **3초** 대기 뒤 인바운드를 처리하고, 루프는 약 **2초**이다. `config/SdvPlatform.conf`에서 FeatureEvaluator는 **feature 목록 마지막**이 유리하다.  
- `ExternalSignals.cmd`는 읽힌 뒤 비워지는 것이 정상이다.  
- `cd` 경로는 본인 WSL 환경에 맞게 수정한다.
