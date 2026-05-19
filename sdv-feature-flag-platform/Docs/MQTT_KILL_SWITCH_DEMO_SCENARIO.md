# MQTT Kill Switch 데모 시나리오

이 문서는 `kill-control-service`가 MQTT로 차량 플랫폼에 `KILL_SWITCH`를 보내고, `MqttVehicleAgentHost`가 이를 받아 실제 Feature 프로세스를 종료하는 데모 시나리오와 실행 방법을 정리한다.

검증 대상 흐름은 아래와 같다.

`REST 요청 -> kill-control-service -> MQTT publish -> MqttVehicleAgentHost -> ExternalSignals_*.cmd -> SignalReceiver -> FeatureEvaluator -> EventBus -> Feature 종료 -> telemetry ACK`

---

## 데모 목표

- 관제 REST API로 `KILL_SWITCH`를 요청한다.
- MQTT 제어 메시지가 `/sdv/vehicle/001/control`로 전달된다.
- 차량 측 `MqttVehicleAgentHost`가 대상 feature를 `config/signals/ExternalSignals_<car_id>.cmd`로 라우팅한다.
- `FeatureEvaluator`가 KillSwitch를 처리해 대상 프로세스를 종료한다.
- 차량 측 telemetry `/sdv/vehicle/001/telemetry`로 `KILL_ACK`를 반환한다.

---

## 데모 대상 예시

- Vehicle ID: `001`
- Target feature: `AdasFeature`
- MQTT broker: `tcp://127.0.0.1:1884`
- REST endpoint: `http://127.0.0.1:18080/api/v1/control/kill-switch`

---

## 사전 준비

아래 구성요소가 준비되어 있어야 한다.

1. `sdv-feature-flag-platform/build/MqttVehicleAgentHost`가 빌드되어 있어야 한다.
2. `kill-control-service`가 빌드 가능해야 한다.
3. WSL에 `mosquitto`, `mosquitto-clients`가 설치되어 있어야 한다.
4. 플랫폼 실행 시 `FeatureEvaluator`, `SignalReceiver`, `SignalSender`, `AdasFeature`가 올라와 있어야 한다.

---

## 실행 순서

### 1. MQTT broker 실행

PowerShell:

```powershell
wsl -e bash -lc "mosquitto -p 1884 -v"
```

정상 예시:

- `Opening ipv4 listen socket on port 1884`
- `mosquitto version 2.0.18 running`

이미 같은 포트가 사용 중이라면 `Address already in use`가 보일 수 있다. 이 경우 기존 broker가 이미 떠 있는 것이므로 새로 띄우지 말고 그대로 사용한다.

### 2. SDV 플랫폼 실행

PowerShell 또는 WSL에서 평소 사용하는 방식으로 플랫폼을 실행한다.

예시:

```powershell
cd "c:\Users\82107\Desktop\Team\sdv-feature-flag-platform"
```

플랫폼이 올라오면 다음이 살아 있어야 한다.

- `FeatureEvaluator`
- `SignalReceiver`
- `SignalSender`
- `AdasFeature`

`[LifeCycleMgr] --- Process Status ---` 로그에서 대상 프로세스가 `running`인지 먼저 확인한다.

### 3. MqttVehicleAgentHost 실행

PowerShell:

```powershell
$env:SDV_MQTT_BROKER_URI="tcp://127.0.0.1:1884"
wsl -e bash -lc "/mnt/c/Users/82107/Desktop/Team/sdv-feature-flag-platform/build/MqttVehicleAgentHost"
```

정상 예시:

- `starting agent`
- `starting: brokerUri=tcp://127.0.0.1:1884`
- `connect complete`
- `subscribing: topic='/sdv/vehicle/001/control/#'`

### 4. kill-control-service 실행

PowerShell:

```powershell
cd "c:\Users\82107\Desktop\Team\kill-control-service"
$env:SDV_MQTT_BROKER_URI="tcp://127.0.0.1:1884"
mvn spring-boot:run
```

정상 예시:

- `Tomcat started on port 18080`
- `Subscribing telemetry: topic=/sdv/vehicle/+/telemetry`

만약 `Port 18080 was already in use`가 보이면 이미 기존 인스턴스가 떠 있는 것이다. 이 경우 중복 실행을 멈추고 기존 서버를 사용한다.

### 5. KillSwitch REST 요청

새 PowerShell 터미널:

```powershell
$body = @{
  vehicleId = "001"
  targetFeature = "AdasFeature"
  timeoutMs = 5000
} | ConvertTo-Json -Compress

Invoke-RestMethod `
  -Method Post `
  -Uri "http://127.0.0.1:18080/api/v1/control/kill-switch" `
  -ContentType "application/json" `
  -Body $body
```

성공 예시:

```text
accepted  : True
vehicleId : 001
command   : KillSwitch
completed : True
detail    : Feature stopped
status    : KILL_ACK
```

---

## 성공 확인 포인트

### 1. `MqttVehicleAgentHost` 로그

아래 흐름이 보여야 한다.

- `/sdv/vehicle/001/control` 메시지 수신
- `stopFeature routed to external signal: KILL:AdasFeature`
- telemetry publish
- `status":"KILL_ACK"`

### 2. `FeatureEvaluator.log`

PowerShell:

```powershell
Get-Content "c:\Users\82107\Desktop\Team\sdv-feature-flag-platform\logs\B\FeatureEvaluator.log" -Tail 20
```

기대 로그:

- `KILL -> AdasFeature`
- `warning -> SignalSender: AdasFeature KillSwitch activated`

### 3. `AdasFeature.log`

PowerShell:

```powershell
Get-Content "c:\Users\82108\Desktop\final_merge\sdv-feature-flag-platform\logs\car_1\AdasFeature.log" -Tail 20
```

기대 로그:

- `Kill switch activated`
- `stopped`

### 4. 플랫폼 상태

`[LifeCycleMgr] --- Process Status ---` 또는 관련 상태 로그에서 아래를 확인한다.

- `AdasFeature : stopped`
- 다른 무관한 feature는 계속 `running`

---

## 판정 기준

아래 네 가지가 모두 만족되면 데모 성공으로 본다.

1. REST 응답에 `completed : True`
2. REST 응답에 `status : KILL_ACK`
3. `FeatureEvaluator.log`에 `KILL -> AdasFeature`
4. `AdasFeature.log` 또는 상태표에 `stopped`

---

## 자주 만나는 문제

### broker 포트 충돌

- 증상: `Address already in use`
- 의미: broker가 이미 떠 있음
- 조치: 새 broker를 띄우지 말고 기존 broker 사용

### kill-control-service 포트 충돌

- 증상: `Port 18080 was already in use`
- 의미: 기존 `kill-control-service` 인스턴스가 이미 실행 중
- 조치: 기존 서버를 사용하거나 기존 프로세스를 종료 후 재실행

### 잘못된 broker 연결

- 증상: 환경변수는 `1884`로 줬는데 로그는 `1883`으로 표시됨
- 의미: 환경변수가 WSL 프로세스까지 전달되지 않았거나 다른 실행본을 보고 있음
- 조치: `MqttVehicleAgentHost` 시작 로그의 `brokerUri=...` 값을 반드시 확인

### PowerShell 붙여넣기 실수

- 증상: `A positional parameter cannot be found that accepts argument '='`
- 의미: 여러 줄 명령이 중간에 붙어서 잘못 실행됨
- 조치: `$body` 생성과 `Invoke-RestMethod` 실행을 분리해서 다시 입력

---

## 데모 한 줄 요약

이 데모는 `kill-control-service`의 REST 요청이 MQTT를 통해 차량으로 전달되고, 차량 측 `MqttVehicleAgentHost`가 기존 `FeatureEvaluator` kill 경로에 연결되어 실제 `AdasFeature` 프로세스를 종료하는지 확인하는 절차다.
