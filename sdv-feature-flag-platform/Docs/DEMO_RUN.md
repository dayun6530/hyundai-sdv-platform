# 데모 실행 가이드 (run_sdv.bat)

이 데모는 “외부 Control-Plane(=Unleash + test UI)”로 Feature를 **켜고(FLAG_ON) / 끄고(FLAG_OFF) / 강제 중단(KillSwitch)** 하는 흐름을 보여줍니다.

## 이 데모가 하는 일 (한 문장)

`run_sdv.bat` 1번 실행으로 아래를 같이 띄우고, test UI 또는 Unleash 설정 변경으로 `Navigation/ADAS/EvEnergy` Feature들을 제어합니다.

> **Note:** 최신 `run_sdv.ps1`는 **`car_1`~`car_10`** 을 순차 기동합니다(`scripts/run_cars_wsl.sh`). 각 차 로그는 `logs/run_sdv/car_N.log`, 차 내부 프로세스 로그는 `logs/car_N/*.log` 입니다. (구버전처럼 `car_1`만 단독 기동하는 동작은 아닙니다.)

## 수동 실행 워크플로 (팀에서 쓰는 순서 예시)

Docker/`run_sdv` 없이 직접 띄울 때 참고용입니다. 경로·포트는 본인 PC에 맞게 바꾸세요.

1. **MQTT 브로커 (예: TCP 1884 / WebSocket 9002)** — `팀프로젝트/mqtt/mosquitto-ws.conf` 사용 시 WSL에서 예:

```bash
mosquitto -c "/mnt/c/Users/82107/Desktop/Hyundai/팀프로젝트/mqtt/mosquitto-ws.conf" -v
```

2. **SDV 플랫폼 (car_1)** — `sdv-feature-flag-platform` 빌드 디렉터리에서 WSL:

```bash
cd "/mnt/c/Users/82107/Desktop/Hyundai/팀프로젝트/sdv-feature-flag-platform"
./build/car_1 config/car/car_1.conf
```

3. **웹(Spring)** — Windows에서 팀 Spring 앱(예: `팀프로젝트/Signal_I_F`)을 IDE 또는 `mvn spring-boot:run` 등으로 기동.

- **브로커를 Docker(`docker-compose`)로만 쓰는 경우**: 호스트는 보통 TCP **1884**, WS **9001** 입니다. 수동 `mosquitto-ws.conf`는 **9002** 등으로 피하는 구성일 수 있어, test UI의 MQTT URL을 그에 맞춥니다.

## 구성요소 연결 관계

- **Unleash (http://localhost:4242)**: Feature flag의 “정답”을 가진 서버
- **FeatureEvaluator (C++)**: Unleash를 주기적으로 폴링해서 토글 상태 변화가 생기면
  - enabled → `EventId::FeatureFlagOn`
  - disabled → `EventId::FeatureFlagOff`
  를 EventBus로 발송
- **EventBus (gRPC)**: 플랫폼 내부 제어/도메인 이벤트 통신 채널
- **Feature (Navigation/ADAS/EvEnergy)**:
  - 프로세스는 계속 살아있고
  - `FLAG_ON`일 때만 도메인 브로드캐스트/처리를 수행
  - `FLAG_OFF`면 다시 대기
- **test UI (http://localhost:5174)**: MQTT(WebSocket)로 `FLAG_ON/FLAG_OFF/KILL` 텍스트를 publish
- **mosquitto**: test UI가 publish한 메시지를 수신하는 MQTT 브로커
- **SignalReceiver (C++)**: MQTT 메시지를 받아 EventBus 제어 이벤트로 변환

## 0) 준비물

- Windows에서 **Docker Desktop 실행 중**
- WSL(Ubuntu) 설치

## 1) Unleash 서버/브로커/DB 기동 (자동)

`run_sdv.bat`가 WSL에서 다음을 자동으로 수행합니다:

- `팀프로젝트/docker-compose.yml`의 서비스 기동
  - `mosquitto`
  - `postgres`
  - `unleash`

> **Broker 모드(추천: A안)**  
> - `run_sdv.ps1`는 데모 안정성을 위해 **WSL에서 수동으로 띄운 `mosquitto`를 종료**한 뒤, **docker-compose의 mosquitto(WS 9001 / TCP 1884)** 를 사용하도록 고정합니다.  
> - 따라서 수동 브로커(예: WS 9002)와 **동시에 쓰지 않는 것**이 전제입니다.

## 2) Unleash UI 접속

- 주소: `http://localhost:4242`
- 최초 계정: `admin` / `admin`

> **Unleash “플래그 만들기 / SDK 연결”은 필수 아님**  
> - **test UI(MQTT)** 로 `FLAG_ON/OFF`만 쓰는 흐름은 Unleash 플래그가 없어도 동작합니다.  
> - **C++ `FeatureEvaluator`가 Unleash 토글을 읽어 EventBus로 반영**하려면, Unleash에 해당 이름의 토글(예: `NavigationFeature`, `AdasFeature`, `EvEnergyFeature`)을 만들어 두는 것이 좋습니다. 없으면 “변경 없음”으로 동작합니다.  
> - 대시보드의 **“SDK 연결”** 안내는 주로 웹/모바일 SDK용입니다. 이 프로젝트는 **HTTP Frontend API + API 키**로 붙습니다(대시보드 카드가 비어 있어도 토큰만 맞으면 됩니다).

## 3) UNLEASH Frontend API Token 발급

Unleash UI에서:
- **API Access → API tokens**
- **Frontend token** 생성
- 값을 복사

## 4) run_sdv.bat 실행 방법

`run_sdv.bat`은 내부적으로 **`run_sdv.ps1`** 을 호출합니다. 한글 경로(`팀프로젝트` 등)에서 `cmd.exe`가 UTF-8 경로를 깨뜨리는 문제를 피하려고, **실제 기동 로직은 PowerShell**에 있습니다. PowerShell에서는 **`.\run_sdv.ps1`** 을 직접 실행해도 동일합니다.

### 방법 A (추천): PowerShell에서 환경변수 설정 후 실행

```powershell
cd "c:\Users\82107\Desktop\Hyundai\팀프로젝트\sdv-feature-flag-platform"
$env:UNLEASH_URL="http://localhost:4242/api/frontend"
$env:UNLEASH_API_KEY="<여기에 Frontend token>"
$env:UNLEASH_APP_NAME="sdv-platform"
.\run_sdv.bat
# 또는 동일: .\run_sdv.ps1
```

### 방법 B: 환경변수 없이 실행 (Unleash 동기화 없이 test UI만 사용)

```powershell
cd "c:\Users\82107\Desktop\Hyundai\팀프로젝트\sdv-feature-flag-platform"
.\run_sdv.bat
```

## 5) test UI로 제어

- 주소: `http://localhost:5174` (안 되면 **`http://127.0.0.1:5174`** 도 같이 시도)
- `run_sdv.bat` 실행 직후 **`'TLOCAL'` / `'EM'` 같은 이상한 명령 오류**가 나오면, `run_sdv.bat` 파일 인코딩이 깨졌을 가능성이 큽니다. 에디터에서 **UTF-8(권장: BOM 없음)** 으로 다시 저장하세요.
- `run_sdv.bat`가 **`/bin/bash: unexpected EOF while looking for matching '\"'`** 를 찍었다면, 예전 버전은 cmd가 `>` 리다이렉트로 깨뜨리던 버그였습니다. 최신 스크립트는 `scripts/wait_unleash_http.sh`로 분리되어 있어야 합니다.
- 브라우저 **`ERR_CONNECTION_REFUSED` (-102)** 는 보통 **5174에 아무 것도 안 떠 있다**는 뜻입니다. `run_sdv.bat`가 한글 경로 때문에 **`PROJECT_DIR`을 비워 둔 채** 끝나면 `START`로 test-ui가 안 뜰 수 있습니다. 최신 `run_sdv.bat`는 `wslpath` 결과를 **임시 파일로 받아** 경로를 잡습니다.
- **MQTT(WebSocket) 브로커 주소**
  - 팀프로젝트 `docker-compose.yml`로 띄운 mosquitto는 보통 **`ws://127.0.0.1:9001`** 입니다(호스트 포트 매핑).
  - Windows에서 **9001이 이미 점유**되는 환경이면, 레포에 `팀프로젝트/mqtt/mosquitto-ws.conf`처럼 **9002로 띄우는 구성**을 쓰기도 합니다. 이 경우 test UI 입력칸을 **`ws://127.0.0.1:9002`** 로 바꾸세요.
- **MQTT(TCP) 플랫폼(WSL) ↔ 브로커(Docker Desktop)**  
  - 브라우저/test UI는 Windows의 `127.0.0.1:9001`로 붙으면 되지만, **WSL 안에서 돌아가는 C++** 는 `127.0.0.1:1884`가 Docker 포워딩과 다를 수 있습니다.  
  - `run_sdv.bat` → `scripts/start_platform_wsl.sh`가 **WSL에서 127.0.0.1:1884가 비어 있으면** 자동으로 **Windows 호스트(게이트웨이)의 `:1884`** 로 `SDV_MQTT_BROKER_URI`를 잡습니다.  
  - WSL에 **직접 mosquitto**를 띄운 경우(같은 VM의 1884가 열려 있으면) 그대로 `127.0.0.1`을 씁니다. 수동으로 고치려면 예: `export SDV_MQTT_BROKER_URI=tcp://172.xx.xx.1:1884` 후 플랫폼 실행.
- 만약 접속이 안 되면(Windows에서 흔함):
  - `run_sdv.bat`가 띄운 `test-ui` 콘솔 창이 **에러로 바로 닫혔는지** 확인하세요.
  - 수동 실행으로 원인 확인:

```powershell
cd "c:\Users\82107\Desktop\Hyundai\팀프로젝트\sdv-feature-flag-platform\test-ui"
npm install
npm run dev
```

- 버튼:
  - `FLAG_ON`: Feature 켜기
  - `FLAG_OFF`: Feature 끄기(프로세스는 유지, 출력만 중단)
  - `KILL`: 해당 Feature 프로세스 stop 요청

## 6) car_1~car_10 + 서브 기능 토글 (요약)

- **기동**: PowerShell에서 프로젝트 루트로 이동 후 `.\run_sdv.ps1` 한 번. (이전 잔류 프로세스는 스크립트가 정리합니다.) WSL 배포판 이름이 다르면 `$env:SDV_WSL_DISTRO="Ubuntu-22.04"` 처럼 지정 가능합니다.
- **데모**: Feature는 기본 **메인 플래그 ON**으로 시작하며, 내비는 **라운드 종료 후에도 자동으로 다음 라운드**를 이어 갑니다(수동 `FLAG_ON` 없이도 위치/도로 이벤트가 계속 나갑니다).
- **서브 기능 MQTT 포맷**: `FLAG_ON:<Feature>.<SUB>` / `FLAG_OFF:<Feature>.<SUB>`  
  - ADAS: `FCA`, `LKA`, `SCC` (예: `FLAG_OFF:AdasFeature.FCA`)  
  - Navigation: `LOCATION`, `ROAD`, `GUIDANCE`  
  - EV Energy: `REGEN`, `RANGE`, `ECO`  
  SignalReceiver가 `AdasFeature.FCA` 를 **`AdasFeature_A.FCA` / `AdasFeature_B.FCA`** 로 확장해 EventBus에 넣습니다.
- **확인 로그**: `logs/car_1/AdasFeature_A.log` 등에서 `SubFlag RX`, `SubFeature ... -> OFF/ON`, 그리고 `FCA: OFF ...` 같은 **도메인 로그**가 이어지는지 확인합니다.
- **test-ui**: `SignalReceiver`가 **`/sdv/vehicle/<car_id>/flags/state`**(JSON, retain)로 논리 플래그 스냅샷을 발행하고, test-ui는 이 토픽을 **구독해서 버튼(active)을 실제 반영 상태와 동기화**합니다. 토픽을 바꾸려면 SignalReceiver 환경변수 `SDV_MQTT_FLAGS_STATE_TOPIC` 을 사용합니다. (Unleash/FeatureEvaluator만으로 바꾼 값은 아직 이 스냅샷에 안 잡히므로, 그 경우는 추후 동일 포맷으로 발행하면 됩니다.)

