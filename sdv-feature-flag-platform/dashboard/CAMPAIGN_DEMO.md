# Campaign UI demo (ADAS v0.5 · car_1)

Terminal-free flow using **Deployment Campaigns** in the dashboard.

## Steps

1. **Select** `campaign_adas_v0.5_car1_canary`
2. **Validate** — fail must be 0
3. **Safety Preconditions** — click **Prepare Demo READY** (sets `deploy/state/<car>.json` OTA fields)
4. **Run Candidate** — requires OTA `update_mode=READY` (use Safety UI, not terminal)
5. **Candidate Logs** — expect shadow / ADAS v0.5 candidate messages
6. **Promote Primary** — after candidate run success (re-run **Prepare Demo READY** if `update_mode` became COMPLETED)
7. **Primary Logs** — expect primary tick OK
8. **Runtime Summary** — Primary v0.5, candidate inactive, config synced
9. **Run History** — candidate + promote success rows
10. **Fleet overview** — click car_1; detail opens directly under the fleet table

## Terminal → UI

| Terminal | UI |
|----------|-----|
| `docker logs car_1_agent_adas` | Campaign Logs → Primary Logs |
| `docker logs car_1_agent_adas_shadow` | Campaign Logs → Candidate Logs |
| `cat deploy/campaign_runs/run_*.json` | Campaign Logs → Campaign Run JSON |
| `tail deploy/history/fleet_history.jsonl` | Campaign Logs → Fleet History |
| `cat deploy/vehicle_runtime/car_1.json` | Vehicle Runtime Summary |

## Advanced (collapsed by default)

- Fleet overview, Unleash controller, legacy fleet-deploy, raw JSONL history, Docker ota_vehicle panel, vehicle shadow/promote actions
