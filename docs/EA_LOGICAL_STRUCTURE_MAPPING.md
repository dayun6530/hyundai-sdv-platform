# EA Logical Structure Mapping

## 1. 왜 실제 코드를 바로 이동하지 않는가

현재 프로젝트는 데모와 self-test가 동작하는 상태이며, 여러 핵심 파일이 기존 경로를 직접 참조한다. 특히 `sdv-feature-flag-platform/status-server/server.js`, `sdv-ota-manager/platform/DeploymentManager`, `sdv-ota-manager/platform/deploy`, `sdv-ota-manager/scripts/ota_vehicle.sh`, `Signal_I_F`는 Dashboard, CLI, Docker runtime demo, MQTT 제어 흐름과 강하게 결합되어 있다.

따라서 Phase 1에서는 실제 코드를 이동하지 않고, EA 기준 논리 폴더와 README만 추가한다. 이 방식은 교수님께 EA 구조를 설명할 수 있게 하면서 현재 데모 흐름을 보존한다.

## 2. 현재 구조와 EA 구조의 차이

현재 구조는 기능 개발과 데모 실행을 중심으로 구성되어 있다. `sdv-feature-flag-platform` 안에는 차량 실행 플랫폼뿐 아니라 `dashboard`, `status-server`도 포함되어 있다. `sdv-ota-manager` 안에는 Vehicle Deployment Manager 역할과 Quality Plane script, Docker runtime demo가 함께 존재한다. `Signal_I_F`는 독립 Spring Boot 프로젝트로 존재하지만 EA 기준으로는 Monitoring Plane의 Signal I/F이면서 Control Plane의 Signal I/O Service와 연결되는 경계 컴포넌트다.

EA 구조는 관심사를 다음처럼 분리한다.

- Vehicle Plane: 차량 내부 HPC 실행, Feature Process, EventBus, LifeCycleManager, Vehicle Deployment Manager.
- Control Plane: Dashboard, Control Server, Feature Deployment Service, Data Monitoring Service, Signal I/O Service.
- DevOps Plane / Quality Plane: source/build/test/package/docker/version registration.
- DevOps Plane / Monitoring Plane: Signal I/F, operation data collection, operation data analysis.
- Registries: Version Registry, Docker Registry Mock.
- Demos: 현재 팀 확장 데모 흐름 보존.

## 3. 기존 경로와 새 논리 경로 매핑표

| 현재 경로 | 새 논리 경로 | 설명 |
|---|---|---|
| `sdv-feature-flag-platform/src/Feature/` | `vehicle-plane/hpc/feature-processes/` | ADAS, Navigation, Energy Feature Process |
| `sdv-feature-flag-platform/src/Platform/LifeCycleManager/` | `vehicle-plane/hpc/feature-flag-platform/` | lifecycle, flag, evaluator |
| `sdv-feature-flag-platform/src/Platform/FeatureCommunication/EventBus/` | `vehicle-plane/hpc/feature-flag-platform/` | EventBus |
| `sdv-feature-flag-platform/src/Platform/CenterCommunication/` | `vehicle-plane/hpc/feature-flag-platform/`, `devops-plane/monitoring-plane/operation-data-collector/`, `control-plane/signal-io-service/` | SignalReceiver, SignalSender, DataCollector, MQTT |
| `sdv-ota-manager/platform/DeploymentManager/` | `vehicle-plane/hpc/vehicle-deployment-manager/` | DeploymentMgr CLI, state, rollback, history, safety checks |
| `sdv-ota-manager/platform/deploy/` | `vehicle-plane/hpc/vehicle-deployment-manager/`, `registries/`, `devops-plane/monitoring-plane/` | version/package/state/history/rollback files |
| `sdv-ota-manager/scripts/ota_vehicle.sh` | `vehicle-plane/hpc/vehicle-deployment-manager/`, `demos/` | Docker Primary/Shadow runtime demo |
| `sdv-feature-flag-platform/dashboard/` | `control-plane/dashboard/` | 관리자 UI |
| `sdv-feature-flag-platform/status-server/` | `control-plane/control-server/`, `feature-deployment-service/`, `data-monitoring-service/` | Node API bridge |
| `Signal_I_F/` | `devops-plane/monitoring-plane/signal-if/`, `control-plane/signal-io-service/` | REST/MQTT Signal I/F |
| `sdv-ota-manager/scripts/new_package.sh` | `devops-plane/quality-plane/release-package-manager/` | package scaffold |
| `sdv-ota-manager/scripts/build_package_image.sh` | `devops-plane/quality-plane/release-package-manager/docker-image-builder/` | build + Docker image |
| `sdv-ota-manager/scripts/update_version_map.py` | `devops-plane/quality-plane/release-package-manager/version-registry-registrar/` | legacy version map update |
| `sdv-feature-flag-platform/tests/` | `devops-plane/quality-plane/quality-test-manager/` | unit tests |
| `sdv-ota-manager/demo_src/` | `demos/` | Docker/OTA demo Feature version sources |

## 4. 용어 정의

- Primary Version: 현재 실제 차량 기능 제어에 사용 중인 안정 버전.
- Candidate Version: 새로 빌드/등록되어 배포 또는 검증 대상이 된 후보 버전.
- Shadow Mode: Candidate Version을 실제 제어 없이 뒤에서 실행하고 로그/동작만 비교하는 방식.
- A/B Rollout: 여러 차량 그룹에 서로 다른 Primary Version을 적용하여 비교하는 Fleet 배포 전략. 기존 코드의 slot/A/B 필드와 동일 개념이 아니며, 새 구조에서는 rollout strategy 용어로만 사용한다.
- Canary: 일부 차량 또는 작은 group부터 시작하는 점진 배포 전략.
- Rollback: 문제가 생기면 이전 Primary Version으로 복귀하는 절차.
- Kill Switch: 위험 상황에서 기능을 즉시 차단하거나 stable fallback으로 전환하는 안전장치.

기존 코드의 `slot`, `A`, `B`, `active_slot`, `fallback_slot` 필드는 즉시 제거하지 않는다. Phase 2 이후 `VersionRegistryAdapter`, `DeploymentStateAdapter`가 기존 필드를 Primary/Candidate 용어로 감싼다.

## 5. Phase 2 이후 실제 리팩토링 순서

1. README 기반 logical mapping을 팀 기준 문서로 확정한다.
2. 기존 파일을 이동하지 않고 wrapper 또는 adapter 파일을 추가한다.
3. Version Registry adapter를 추가해 legacy `version_map.json`을 Primary/Candidate 모델로 노출한다.
4. Deployment State adapter를 추가해 `active_*`, `primary_*`, `pending_active_*`, `shadow_*`를 안정적인 API로 감싼다.
5. `status-server/server.js`를 route/service 단위로 내부 분리한다.
6. Dashboard에서 slot 용어를 UI adapter로 숨기고 Primary/Candidate 표현을 적용한다.
7. `sdv-ota-manager`의 DeploymentManager 역할을 Vehicle Deployment Manager 하위 컴포넌트로 문서/코드 분리한다.
8. `Signal_I_F`는 API/MQTT schema를 유지한 채 Signal I/O Service adapter와 연결한다.
9. scripts를 Quality Plane wrapper로 감싸고 CI/CD pipeline으로 확장한다.
10. 모든 build/test/demo가 통과한 뒤 물리 이동 여부를 결정한다.

## 6. 교수님 데모에서 설명할 문장

현재 프로젝트는 실제 동작하는 데모 흐름을 보존하기 위해 기존 코드를 바로 이동하지 않았습니다. 대신 EA 기준으로 Vehicle Plane, Control Plane, DevOps Plane의 논리 폴더를 만들고, 각 폴더의 README에서 현재 구현 코드가 어디에 있는지 매핑했습니다. 따라서 교수님께는 EA 구조와 실제 코드 위치를 함께 보여줄 수 있고, 이후 단계에서 adapter를 추가해 Primary/Candidate, Shadow Mode, Canary, Rollback, Kill Switch 구조로 점진 리팩토링할 수 있습니다.
