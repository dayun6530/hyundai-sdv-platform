# SDV Feature Flag Platform - Architecture & Vision

## Requirements

- **OS**: Linux
- **Compiler**: GCC 7+ 또는 Clang 5+ (C++17 지원)
- **Build system**: CMake 3.16+
- **Dependencies**: gRPC, Protobuf

## Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./SdvPlatform ../config/SdvPlatform.conf
```

## Project Purpose

자동차(SDV) 소프트웨어를 Feature 단위로 통합 관리하는 **Feature Flag Platform** 개발.

현대자동차와 같은 OEM은 이미 수백 종의 **단일 소프트웨어**(Feature)를 보유하고 있다.
Navigation 하나만 해도 300만 LoC가 넘는 대규모 소프트웨어가 존재한다.
이런 기존 소프트웨어를 FF Platform에 맞춰 다시 개발하는 것은 현실적으로 불가능하다.

따라서 이 플랫폼의 접근법은:

1. **기존 소프트웨어를 Feature로 감싼다** - 기존 소프트웨어를 수정하지 않고, Platform의 Feature 클래스로 래핑하여 통합한다.
2. **Platform이 Feature의 생명주기를 관리한다** - 실행, 종료, 모니터링, 재시작 등.
3. **Feature 간 통신을 Platform이 중개한다** - Event-Bus를 통한 느슨한 연결.
4. **Feature Flag 기능으로 Feature를 동적 제어한다** - Shadow Mode, Kill Switch, A/B Testing 등.

## Architecture Principles

### 1. Loose Coupling (느슨한 결합)

Platform의 핵심 컴포넌트들은 서로 느슨하게 연결되어야 한다.
특히 **LifeCycleMgr와 Event-Bus의 연동**은 인터페이스 기반으로 분리되어야 한다.

현재 Event-Bus는 자체 구현한 gRPC 기반 프로토타입이지만,
향후 **DDS(Data Distribution Service)** 같은 산업용 오픈소스 미들웨어로 교체 가능해야 한다.
교체 시 LifeCycleMgr이나 Feature 코드에 영향이 없도록 설계해야 한다.

> **주의: gRPC 종속 구조**
> 현재 `EventBusHost`는 gRPC가 `.proto`에서 자동 생성하는 `EventBus::Service`를 상속받고,
> `EventBusProxy`는 자동 생성된 `EventBus::Stub`을 멤버로 사용한다.
> 이 두 클래스는 gRPC 프레임워크에 직접 종속되어 있으므로,
> DDS 등으로 교체 시 **Communications/EventBus/ 디렉토리 전체가 교체 대상**이다.
> 반면 `EventBusInterface`는 우리가 정의한 순수 가상 클래스이므로,
> 새로운 통신 미들웨어도 이 인터페이스를 구현하면 Feature 코드는 변경 없이 동작해야 한다.
> `Communications/` 디렉토리 구조는 이러한 교체를 용이하게 하기 위해 분리한 것이다.

### 2. Wrapping, Not Rewriting (래핑, 재작성 아님)

기존 자동차 소프트웨어(수백 종, 수백만 LoC)를 다시 개발하는 것이 아니라,
Platform의 Feature 클래스가 이들을 **감싸서** 관리하는 구조이다.
Feature 클래스는 기존 소프트웨어의 실행/종료/통신을 대행하는 얇은 래퍼 역할을 해야 한다.

### 3. Process Isolation (프로세스 격리)

모든 Feature는 **독립된 프로세스**로 실행된다.
이는 안전성, 장애 격리, 그리고 Feature Flag 기능(Shadow Mode, Kill Switch)을 위해 필수적이다.

### 4. Configuration-Driven (설정 기반)

Feature 목록은 하드코딩하지 않고 설정 파일에서 읽어온다.
수백 개의 Feature가 추가될 수 있으므로 확장성이 핵심이다.

### 5. Prototype First (프로토타입 우선)

현재는 **컨셉 증명(PoC)** 단계이다.
핵심 구조와 인터페이스를 먼저 잡고, 이후 점진적으로 기능을 확장한다.

## Communication Strategy

플랫폼 내부 통신 방식을 단순하게 유지한다. 혼란을 방지하기 위해 최소한의 통신 메커니즘만 사용한다.

| 통신 방식 | 용도 | 참여자 |
|----------|------|--------|
| **EventBus (gRPC)** | Feature 간 내부 통신, Flag 제어 | Feature들, FeatureEvaluator, WatchDog |
| **파일 (cmd)** | Daemon ↔ Feature 데이터 전달 | SignalReceiver ↔ FeatureEvaluator ↔ SignalSender |
| **HTTP** | 외부 Control Center 연동 | SignalReceiver, SignalSender, DataCollector |

### FeatureEvaluator의 위치

FeatureEvaluator는 외부 신호를 수신(파일)하여 내부 EventBus로 flag on/off, kill switch를 전송하는 **경계점(bridge)** 역할이다.
Feature를 상속받아 EventBus에 연결되며, LifeCycleMgr가 feature 타입으로 관리한다.

### SignalReceiver/SignalSender/DataCollector는 Feature가 아니다

이 컴포넌트들은 외부 시스템과 통신하는 **인프라 Daemon**이다.
EventBus와 무관하므로 Feature를 상속받지 않으며, LifeCycleMgr가 communication 타입으로 관리한다.

> **KillSwitch 실시간성 제약**
> 현재 EventBus는 폴링 기반(1~2초 주기)이므로, KillSwitch 등 긴급 명령의 즉시 전달이 보장되지 않는다.
> 외부 센터 경유 경로(HTTP → 파일 → EventBus)의 총 지연은 최대 7초에 달할 수 있다.
> 단, 외부 센터 명령은 네트워크 지연이 이미 포함되어 있으므로 프로토타입에서는 수용 가능하다.
> 진짜 긴급 상황은 LifeCycleMgr가 SIGTERM/SIGKILL로 프로세스를 직접 종료할 수 있다.
> DDS 교체 시 QoS 우선순위 기능으로 이 지연 문제가 해결될 예정이다.

## Component Responsibilities

| Component | Responsibility |
|-----------|---------------|
| **LifeCycleMgr** | Feature 프로세스 생성/종료/모니터링. Event-Bus와 느슨하게 연동. |
| **Feature (base)** | 기존 소프트웨어를 래핑하는 부모 클래스. 로깅, EventBus 연결 등 공통 기능. |
| **EventBusInterface** | Event-Bus 추상화 인터페이스. 구현체 교체 가능. |
| **EventBusHost** | 현재 gRPC 기반 Event-Bus 서버 (교체 대상). `FeatureCommunication/EventBus/` |
| **EventBusProxy** | 현재 gRPC 기반 Event-Bus 클라이언트 (교체 대상). `FeatureCommunication/EventBus/` |
| **WatchDog** | Feature로서 EventBus 이벤트를 감시하는 순수 감시자. 이상 탐지, 로깅 담당. |
| **FeatureEvaluator** | Feature 상속. SignalReceiver로부터 파일로 신호 수신 → EventBus로 flag on/off, kill switch 직접 전송. |
| **Signal** | SignalReceiver/SignalSender 부모 클래스. HTTP 프로토콜 추상화 + SignalData 엔티티. |
| **SignalReceiver** | Signal 상속. 외부 센터 → 인바운드 신호 수신. `CenterCommunication/` |
| **SignalSender** | Signal 상속. 아웃바운드 warning/status → 외부 센터 전송. `CenterCommunication/` |
| **DataCollector** | Daemon. Feature 로그 파일 수집 및 센터 전송. `CenterCommunication/` |

## Future Feature Flag Capabilities

- **Shadow Mode**: Feature를 실제 차량 제어 없이 시뮬레이션 실행
- **Kill Switch**: 문제 발견 시 특정 Feature 즉시 중단
- **A/B Testing**: 동일 Feature의 다른 버전을 선택적으로 실행
- **Gradual Rollout**: Feature를 차량군에 점진적으로 배포

## Package Structure

디렉토리와 네임스페이스가 일치하도록 구성한다.

```
src/Platform/
├── FeatureCommunication/EventBus/  → sdv::platform::communications::eventbus
│   ├── include/FeatureCommunication/EventBus/
│   ├── src/
│   └── proto/
├── LifeCycleManager/               → sdv::platform::lifecyclemgr
│   ├── include/LifeCycleManager/
│   └── src/
└── CenterCommunication/            → sdv::platform::centercommunication
    ├── include/CenterCommunication/
    └── src/

src/Feature/                        → sdv::feature
```

| 네임스페이스 | 역할 | 교체 가능성 |
|---|---|---|
| `sdv::platform::communications::eventbus` | gRPC 기반 Feature 통신 미들웨어 | **교체 대상** (DDS 등) |
| `sdv::platform::lifecyclemgr` | Feature 생명주기 관리, Flag, FeatureEvaluator | 안정 (변경 최소화) |
| `sdv::platform::centercommunication` | 외부 센터 연동 (Signal, DataCollector) | 프로토콜 교체 가능 (HTTP 등) |
| `sdv::feature` | 개별 Feature 구현 | 지속 확장 |

## Coding Conventions

- **Class names**: PascalCase (e.g., `EventBusHost`)
- **File names**: PascalCase, matching class name (e.g., `EventBusHost.h`)
- **Top-level directories**: 소문자 (e.g., `src/`, `tests/`, `config/`, `markdown/`, `logs/`)
- **Source sub-directories**: PascalCase, 약어 금지 (e.g., `LifeCycleManager`, `FeatureCommunication/EventBus`, `CenterCommunication`)
- **Executable names**: PascalCase (e.g., `SdvPlatform`, `EventBusHost`, `AdasFeature`)
- **Config files**: PascalCase (e.g., `SdvPlatform.conf`)
- **Function names**: camelCase (e.g., `sendEvent`)
- **Member variables**: camelCase (e.g., `componentId`)
- **금지**: snake_case (언더스코어 `_` 구분자 사용 금지)
- Target platform: Linux
- Language: C++17
- Build system: CMake 3.16+
- IPC: gRPC (prototype, replaceable)

> **디렉토리 네이밍 원칙**: 프로젝트 최상위 디렉토리(`src/`, `tests/`, `config/`, `logs/` 등)는
> 일반적인 IDE 도구 관례에 따라 **소문자**로 시작한다.
> 반면 소스 코드 내부의 서브 디렉토리(`src/Platform/FeatureCommunication/EventBus/` 등)는
> 네임스페이스와 일치시키기 위해 **PascalCase**를 사용한다.
