# SDV Admin Portal

통합 관리자 웹 UI. 사이드 메뉴로 다음 뷰를 전환합니다 (iframe 임베드):

| 메뉴 | URL | 비고 |
|------|-----|------|
| Deployment / Release | http://127.0.0.1:5173 | `dashboard/` Vite 앱 |
| Test UI | http://127.0.0.1:5174 | MQTT 시뮬레이터 |
| PostgreSQL | http://127.0.0.1:5050 | pgAdmin (Docker) |
| Grafana | http://127.0.0.1:3000 | 로그/메트릭 (Docker) |

**포함하지 않음:** Qt `SDV_Dashboard.exe` (`sdv-dashboard-qt`), Cockpit UI (`:5175`).

## 실행

`run_sdv.ps1` 또는 `run_sdv2.ps1`이 `admin-portal`을 함께 띄웁니다.

```powershell
cd admin-portal
npm install
npm run dev
```

브라우저: **http://127.0.0.1:5170** (`localhost:5170` 사용 금지 — pgAdmin/Grafana iframe 오류 원인)

- **Dashboard / Test UI**: `5173`, `5174` iframe (직접 URL)
- **pgAdmin / Grafana**: iframe 없음 → **새 탭** (프록시 실패 시 포털이 3중 중첩되던 문제 방지)

## Docker (Grafana / pgAdmin iframe)

`docker-compose.yml`에 임베드 허용 설정이 있습니다. 기존 컨테이너가 있으면 재생성하세요:

```bash
docker compose up -d grafana pgadmin
```
