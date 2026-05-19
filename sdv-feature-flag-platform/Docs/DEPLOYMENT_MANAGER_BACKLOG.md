# 배포관리(Deployment Manager) — 백로그

파일 기반 배포·버전 선택·config 갱신·롤백을 담당하는 `DeploymentMgr` 및 연동(`LifeCycleMgr::resolveExecPath`, `deploy/*`)에 대한 **완료 항목**과 **후속 백로그**를 정리한다.

---

## 스프레드시트 한 줄 복사용 (TSV)

아래 **한 줄**을 스프레드시트에 붙여 넣으면 열이 나뉜다. (탭으로 구분)

```text
6	Platform / Release	파일 기반 배포관리(DeploymentMgr)	빌드 산출물을 deploy/packages → deploy/active로 복사하고, deploy/active_selection.conf + ConfigUpdater로 config의 feature 목록을 ADAS/NAV/ENERGY별 A|B에 맞게 갱신한다. LifeCycleMgr는 deploy/active/<실행파일>이 있으면 우선 실행한다. 롤백은 도메인별 스택(deploy/stacks)으로 이전 선택을 복원한다. (OTA/서명/네트워크 전송은 범위 밖.)	"1. 리포 루트에서 빌드: cmake -S . -B build && cmake --build build -j
2. ./build/DeploymentMgr status 로 선택값·경로 확인
3. cp build/AdasFeature deploy/packages/ && ./build/DeploymentMgr deploy AdasFeature
4. ./build/DeploymentMgr --config config/car_1.conf sync-config 후 config에 AdasFeature 줄이 반영되는지 확인
5. ./build/car_1 기동 후 logs/car_1에서 fleet 버전 로그 확인
6. ./build/DeploymentMgr --config config/car_1.conf rollback ADAS 후 car_1 재시작 시 이전 버전으로 돌아가는지 확인"
```

---

## 에픽

| 에픽 ID | 이름 | 목표 |
|--------|------|------|
| DM-1 | 패키지 수신·배포 | 새 바이너리를 지정 폴더에 두고 active로 승격 |
| DM-2 | 버전·이력 | 배포·선택·롤백을 추적 가능하게 남김 |
| DM-3 | 실행 연동 | LifeCycleMgr가 배포 경로를 우선 사용, config와 일치 |
| DM-4 | 운영 UX | CLI·문서·멀티 config·재시작 힌트 등 |

---

## 제품 백로그 (우선순위 제안)

### 완료 (Done) — 현재 코드 기준

| ID | 유형 | 항목 | 비고 |
|----|------|------|------|
| DM-1.1 | Story | `deploy/packages` → `deploy/active` 바이너리 복사 | `DeploymentMgr deploy <이름>` |
| DM-1.2 | Story | 배포 결과를 `deploy/history/deployment.log`에 기록 | 파이프 구분 필드 |
| DM-1.3 | Story | `deploy/manifest.txt`에 배포 요약 append | `list`로 tail 확인 |
| DM-2.1 | Story | 도메인(ADAS/NAV/ENERGY)별 활성 A\|B를 `deploy/active_selection.conf`에 저장 | KEY VALUE 형식 |
| DM-2.2 | Story | `select` / `rollback` 시 이력 로그 남김 | selection / rollback 액션 |
| DM-2.3 | Story | 롤백용 도메인 스택 `deploy/stacks/<DOMAIN>.stack` | 최소 2단계 이상일 때 pop |
| DM-3.1 | Story | `config/SdvPlatform.conf` 등에서 관리 대상 `feature` 블록 자동 재작성 | `ConfigUpdater::applyToConfig` |
| DM-3.2 | Story | `LifeCycleMgr`가 `deploy/active/<exec>`를 우선 실행 | cwd 기준 경로 |
| DM-3.3 | Story | `--config`로 갱신 대상 platform 설정 파일 지정 | car_1 .. car_10 |
| DM-4.1 | Task | 예시 바이너리 A/B 로그 구분 (`[ADAS-A]` / `[ADAS-B]` 등) | Feature 소스 |

### 백로그 (To do / 향후)

| ID | 우선순위 | 유형 | 항목 | 수용 기준(요약) |
|----|----------|------|------|-----------------|
| DM-5.1 | P1 | Story | **롤백/선택 후 플랫폼 재기동 가이드 자동 안내** | `select`/`rollback` 성공 시 stdout에 “LifeCycleMgr 재시작 필요” 문구 |
| DM-5.2 | P1 | Story | **Shadow 바이너리 일괄 배포** | `deploy AdasFeature` 시 `AdasFeatureShadow` 동시 복사 옵션 또는 서브커맨드 |
| DM-5.3 | P2 | Story | **이력 포맷 사람 친화적** | `deployment.log`에 헤더 줄 추가 또는 `DeploymentMgr log`로 예쁘게 출력 |
| DM-5.4 | P2 | Story | **car_1..car_10 동시 운영 시 deploy 루트 분리** | `--deploy-root` 또는 `SDV_DEPLOY_ROOT`로 selection/active 경로 분리 |
| DM-5.5 | P2 | Task | **패키지 무결성** | SHA256 체크섬 파일 검증(선택) |
| DM-5.6 | P3 | Story | **dry-run 모드** | config/복사 없이 변경 예정만 출력 |
| DM-5.7 | P3 | Epic | **원격 OTA** | HTTP 업로드, 서명, 단계적 롤아웃 — 별도 에픽 |

---

## 용어 (초보자용)

| 용어 | 의미 |
|------|------|
| 배포(deploy) | 실행 파일을 `packages`에서 `active`로 복사해 “실행 준비 위치”에 두는 것 |
| 선택(select) | 어떤 A/B 라인을 띄울지 **config + active_selection**에 반영하는 것 (프로세스 자동 재시작은 아님) |
| 롤백(rollback) | 도메인별로 **이전에 쌓인 선택 스택**에서 한 단계 되돌리는 것 |
| LifeCycleMgr | config를 읽어 **실제로 프로세스를 fork** 하는 실행 주체 |

---

## 관련 경로

- CLI 소스: `src/Platform/DeploymentManager/`
- 실행 파일(빌드 후): `build/DeploymentMgr`
- 상태·이력: `deploy/active_selection.conf`, `deploy/history/deployment.log`, `deploy/stacks/`

---

## 참고

- Kill 스위치 등 **런타임 Kill**과 **DeploymentMgr select**는 별개다. Kill은 프로세스 종료, select는 설정 변경이다.
- 롤백 직후 **이미 떠 있는** `car_1` 등 LifeCycle 인스턴스는 옛 설정으로 계속 돌아갈 수 있으므로, **재시작** 후 `logs/car_<n>/` 로그로 확인하는 것이 안전하다.
