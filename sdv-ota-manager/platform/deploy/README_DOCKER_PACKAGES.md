## 목적

이 디렉터리(`sdv-ota-manager/platform/deploy/`)는 **차량(HPC) 내부 배포 상태/이력/패키지 저장소**를 위한 파일 기반 저장소입니다.

우리 팀의 최종 방향은 플랫폼 전체를 교체하는 방식이 아니라, **Feature 단위 Docker 이미지**를 배포하는 것입니다.

- 예: `sdv/adas-feature:v2.0`
- 예: `sdv/navigation-feature:v2.0`
- 예: `sdv/evenergy-feature:v2.0`

이 문서는 현재 저장소에 공존하는 두 세계(레거시 실행파일 기반 + Docker 이미지 기반)를 구분해 기록합니다.

## 관련 디렉터리/파일 (현재 코드 기준)

- `../../build_artifacts/` (중요)
  - **Docker 이미지 자체가 저장된 곳이 아닙니다.**
  - Feature별/버전별로 `Dockerfile` + (빌드된) 실행파일 + `checksum.sha256` 같은 **이미지 빌드 입력(산출물)**이 들어 있습니다.
  - 실제 Docker 이미지는 각 PC에서 `docker build ...` 또는 스크립트 실행으로 생성됩니다.
- `packages/`: 패키지(버전별) 보관 위치
  - 예: `platform/deploy/packages/ADAS/v2.0/`
  - `Dockerfile`, `manifest.json`, `checksum.sha256` 등이 생성될 수 있음
- `active/`: “현재 실행 대상으로 간주되는” 아티팩트(레거시: 실행파일) 위치
- `state/`: 차량별 fleet 상태(JSON)
  - 예: `platform/deploy/state/car_1.json`
- `rollback/`: 롤백 메타데이터(JSON)
- `history/`: 배포/운영 이력 로그
- `version_map.json`: feature/slot(A/B) → version/binary 등 매핑(레거시 실행파일 흐름에서 강함)
- `package_registry.json`: 패키지 레지스트리(현재는 빈 스켈레톤, UI/서버가 업데이트 가능)

## Docker 이미지 패키지의 “메타데이터(권장)”

`packages/<DOMAIN>/<version>/manifest.json`는 현재 `scripts/new_package.sh`/Status Server에서 만들어지며,
Docker 이미지 단위 배포를 표현하기 위해 최소한 아래 필드가 있으면 좋습니다.

```json
{
  "domain": "ADAS",
  "feature": "AdasFeature",
  "version": "2.0",
  "artifact_type": "docker_image",
  "docker_image": "sdv/adas-feature:v2.0",
  "image_digest": "sha256:<filled_after_docker_build>",
  "checksum_file": "checksum.sha256",
  "executable_name": "AdasFeature",
  "supported_targets": ["docker"],
  "default_role": "candidate",
  "created_at": "2026-05-06T00:00:00Z"
}
```

주의:
- 이 단계에서는 `DeploymentManager`가 Docker 이미지를 빌드/실행하지 않습니다.
- Docker 런타임 OTA 데모 상태는 `sdv-ota-manager/deploy/vehicle_runtime/*.json`에 별도로 기록됩니다.
- **`checksum_file`은 Docker 이미지 자체 검증이 아닙니다.**
  - 현재 체크섬은 주로 패키지 디렉터리의 **이미지 빌드 컨텍스트에 포함되는 실행파일/산출물**을 검증하기 위한 보조 수단입니다.
  - Docker 이미지 기준의 1순위 무결성/고정 값은 `image_digest`입니다.

---

## Sub-feature 확장 방향 (설계 메모)

현재 배포 단위는 **domain-level feature 이미지(ADAS / Navigation / Energy)** 입니다.
하지만 교수님 피드백 기준으로 Navigation 내부의 `realtime_route`, `voice_guidance`, `charging_recommend` 같은 세부 기능도
향후에는 **독립 Feature Docker 이미지/독립 버전관리 대상**이 될 수 있어야 합니다.

이번 단계에서는 실제로 sub-feature를 분리/빌드/배포하지 않고, 패키지 `manifest.json`에 아래 optional 필드를 추가로 허용합니다:
- `feature_scope`: `"domain"` | `"sub_feature"`
- `sub_feature`: 예) `"realtime_route"`, `"voice_guidance"`, `"charging_recommend"`

운영 원칙(현재 단계):
- `feature_scope`가 없으면 기본값은 `"domain"`으로 해석합니다(기존 manifest와 호환).
- sub-feature가 독립 배포 단위가 되더라도, `deploy/state`/UI/CLI의 상위 domain(예: Navigation)과의 매핑은 유지되어야 합니다.

---

## Progressive Rollout (canary → beta → stable) 메타데이터(선택)

Fleet 대상(group/region/vehicle) 배포 기능은 이미 구현되어 있으며(DeploymentManager `fleet-deploy`),
운영 측면에서 canary → beta → stable 순서로 Shadow→검증→Promote 하는 점진적 배포를 권장합니다.

이번 단계에서는 자동 스케줄러/잡큐를 만들지 않고, 추후 확장 대비용으로 `manifest.json`에 아래 optional 필드를 허용합니다:
- `rollout_stage`, `rollout_group`, `rollout_order`
- `promotion_required`
- `rollout_policy` (자유 형식 객체)

자세한 운영 흐름은 `deploy/README_PROGRESSIVE_ROLLOUT.md`를 참고하세요.

## `package_registry.json`에 들어갈 수 있는 항목(예시)

`package_registry.json` 자체 포맷은 이번 단계에서 변경하지 않습니다.
다만 Docker 이미지 기반 배포를 위해 아래처럼 “이미지 식별자”를 명시하는 형태를 권장합니다.

```json
{
  "schema_version": 1,
  "packages": [
    {
      "package_id": "ADAS_2.0_20260506_153000",
      "feature": "ADAS",
      "version": "2.0",
      "role": "candidate",
      "binary": "AdasFeature",
      "description": "ADAS feature image v2.0",
      "compatible_models": ["GEN1"],
      "compatible_regions": ["KR"],
      "requires_safety_check": true,
      "install_policy": {
        "allow_driving_download": false,
        "install_requires": {
          "vehicle_mode": "PARKED",
          "ignition": "OFF",
          "update_mode": "READY",
          "gear": "P",
          "speed": 0,
          "parking_brake": "ON",
          "min_battery": 30,
          "driver_approved": true,
          "network": "CONNECTED",
          "target_system_running": false
        }
      },
      "rollback": { "fallback_slot": "A", "fallback_required": true },

      "artifact_type": "docker_image",
      "docker_image": "sdv/adas-feature:v2.0",
      "image_digest": "sha256:<optional>"
    }
  ]
}
```

위의 추가 필드(`artifact_type`, `docker_image`, `image_digest`)는 “Docker 이미지가 배포 단위”임을 메타데이터에서
명확히 하기 위한 것입니다. 이 단계에서는 운영 코드(서버/UI/CLI)의 실제 적용 로직을 변경하지 않습니다.

## Field definitions (manifest + registry)

이 프로젝트는 아직 UI/서버가 Docker 필드를 강제 사용하지 않으므로, 아래 필드는 “스키마 기준”으로만 정의합니다.
실제 적용/검증(예: digest 자동 기록, checksum 검증)은 7단계에서 다룹니다.

- **manifest.json**: `deploy/packages/<DOMAIN>/v<version>/manifest.json` (패키지 단위 메타데이터)
- **package_registry.json**: `deploy/package_registry.json` (패키지 검색/선택을 위한 인덱스)

자세한 필드 의미/권장값 표는 `deploy/package-metadata-fields.canvas.tsx`와 스키마 파일을 참고하세요.

---

## build_artifacts → 공식 패키지(packages)로 “등록”하는 방법

### 원칙(역할 분리)

- `build_artifacts/`
  - **빌드 결과물 / Docker 이미지 빌드 입력(build context) 산출물**
  - 예: `Dockerfile`, 실행파일, `checksum.sha256`
  - **여기에 Docker 이미지가 저장되는 것이 아닙니다.**
- `platform/deploy/packages/`
  - **배포관리(DeploymentManager/Status Server)가 참조하는 “공식 패키지 저장소”**
  - 최소 구성: `manifest.json`, `Dockerfile`, `checksum.sha256`, 실행파일(또는 build context)

### 이미 존재하는 흐름(권장)

1) `scripts/new_package.sh`로 공식 패키지 디렉터리 생성(메타데이터/도커파일)  
2) `scripts/build_package_image.sh`로 플랫폼 빌드 → 실행파일 복사 → 체크섬 → `docker build` → (optional) digest 기록

예시:

```bash
./scripts/new_package.sh --domain ADAS --feature AdasFeature --version 2.0
./scripts/build_package_image.sh --domain ADAS --feature AdasFeature --version 2.0
```

### build_artifacts가 이미 있는 경우(최소 helper)

이미 `build_artifacts/<domain>/v<version>/`에 `Dockerfile`/실행파일/`checksum.sha256`가 있다면,
아래 helper로 공식 패키지 폴더로 복사 + `manifest.json` 필수 필드를 보완할 수 있습니다.

```bash
./scripts/register_build_artifact.sh --domain ADAS --version 2.0
```

옵션:
- `--feature <ExecutableName>`: 실행파일 이름 자동 탐지가 실패하면 지정
- `--source <path>`: build_artifacts 경로가 기본값이 아닐 때 지정

이 helper는 **docker build를 실행하지 않습니다.** (이미지는 각 PC에서 별도로 빌드해야 함)

Docker image 자동 생성 규칙(이 helper 기준):
- domain이 `ADAS`(대소문자 무관) → `sdv/adas-feature:v<version>`
- domain이 `Navigation` 또는 `nav` → `sdv/navigation-feature:v<version>`
- domain이 `Energy`/`EvEnergy`/`EVENERGY`/`evenergy`/`ev` → `sdv/evenergy-feature:v<version>`
- version은 `2.0` → `v2.0`, `v2.0` → `v2.0` (중복 v 방지)

