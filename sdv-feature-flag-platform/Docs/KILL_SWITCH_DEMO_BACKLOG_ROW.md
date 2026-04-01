# Kill Switch(긴급 중단) 데모 — 백로그/시트 복사용

아래 **한 줄(TSV)** 을 스프레드시트에 그대로 붙여 넣으면 열이 나뉩니다. (탭으로 구분됨)

```text
5	Feature Management	Kill Switch(긴급 중단) 데모	운전자/차량 위험 상황(예: Driver Monitoring System: 운전자 수면, ADAS 치명적 오류)이 발생하면, 관제에서 내린 긴급 명령이 SignalReceiver로 수신되어 `logs/SignalsInbound.cmd`로 전달되고, FeatureEvaluator가 이를 `EventId::KillSwitch`(12)로 매핑하여 특정 Feature(예: AdasFeature)를 협력적 종료(cooperative stop)한다. (CONVENTIONS: 외부 `SignalData` 한 줄은 `type:target:payload` 3필드. 평가기가 KillSwitch로 변환하는 액션명은 `KILL`뿐이므로, DMS 등 도메인 사유는 `payload`에 둔다.)	"1. 프로젝트 루트에서 SdvPlatform 실행. EventBusHost, SignalReceiver, SignalSender, FeatureEvaluator, AdasFeature가 기동되었고, [LifeCycleMgr] --- Process Status --- 에서 running임을 확인한다.
(cd /mnt/c/Users/82107/Desktop/팀플/sdv-feature-flag-platform && ./build/SdvPlatform)
2. 외부 상황 시그널 주입(터미널 2, WSL). ADAS 긴급 중단을 `KILL`로 흉내 내고, 사유는 세 번째 필드에 싣는다.
(printf 'KILL:AdasFeature:DMS_SLEEP\n' >> config/ExternalSignals.cmd)
3. 성공 확인(SdvPlatform이 출력하는 터미널 / 필요 시 logs/*.log)
- [SignalReceiver] Received: KILL:AdasFeature:DMS_SLEEP 확인
- Event Information(ID: 12, Message: AdasFeature) 확인
- [LifeCycleMgr] 상태표에서 AdasFeature : stopped 확인
- logs/FeatureEvaluator.log 또는 콘솔에서 KILL -> AdasFeature 흐름 확인
- [SignalSender] Sending to center: WARNING:AdasFeature:KillSwitch activated 확인
- NavigationFeature 등 다른 feature는 running 유지 확인"
```

---

## 스프레드시트에 안 붙을 때 (수동 복사)

| 항목 | 내용 |
|------|------|
| ID | 5 |
| 영역 | Feature Management |
| 제목 | Kill Switch(긴급 중단) 데모 |
| 설명 | 운전자/차량 위험 상황(예: DMS 운전자 수면, ADAS 치명적 오류) 시 관제 명령이 SignalReceiver → `SignalsInbound.cmd` → FeatureEvaluator를 거쳐 `KillSwitch`(12)로 매핑되고, 대상 Feature는 `requestStop()` 기반 협력적 종료. 프로토타입에서는 외부 한 줄이 `KILL:FeatureName:사유` 형태여야 평가기가 동작한다. |
| 검증 절차 | 아래 블록 전체를 한 셀에 넣어도 됨 |

**검증 절차 (복사용)**

1. 프로젝트 루트에서 SdvPlatform 실행. EventBusHost, SignalReceiver, SignalSender, FeatureEvaluator, AdasFeature가 기동되었고, `[LifeCycleMgr] --- Process Status ---`에서 running인지 확인한다.

```bash
cd /mnt/c/Users/82107/Desktop/팀플/sdv-feature-flag-platform && ./build/SdvPlatform
```

2. 외부 시그널 주입(터미널 2, WSL).

```bash
printf 'KILL:AdasFeature:DMS_SLEEP\n' >> config/ExternalSignals.cmd
```

3. 성공 확인

- `[SignalReceiver] Received: KILL:AdasFeature:DMS_SLEEP`
- `Event Information(ID: 12, Message: AdasFeature)`
- `[LifeCycleMgr]   [feat] AdasFeature (PID:...) : stopped`
- `logs/FeatureEvaluator.log`에 Kill 처리 흔적(예: `KILL -> AdasFeature`)
- `[SignalSender] Sending to center: WARNING:AdasFeature:KillSwitch activated`
- 다른 feature는 `running` 유지

---

## 참고

- 윈도우 사용자 경로가 다르면 `cd` 줄만 본인 경로로 바꾼다.
- `SignalReceiver` 폴링 주기(코드상 3초), `FeatureEvaluator` 처리 주기(2초) + 시작 시 3초 대기가 있으므로, 주입 후 수 초 안에 로그가 이어지면 정상이다.
- 예시의 `DMS_SLEEP`은 **payload(사유)** 이다. 첫 필드를 `DMS_SLEEP`으로 바꾸면 현재 `FeatureEvaluator`는 처리하지 않는다.
