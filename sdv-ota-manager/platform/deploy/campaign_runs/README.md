# Campaign runs

A **campaign run** records one execution of a campaign phase (`candidate` or future `promote`). Each `campaign-run` writes a JSON file under this directory.

## Campaign runs vs fleet history

| Store | Scope | Written by |
|-------|--------|------------|
| `deploy/history/fleet_history.jsonl` | Every fleet-deploy attempt (all features, all scopes) | `fleet-deploy`, `campaign-run` (via same deploy path) |
| `deploy/campaign_runs/run_*.json` | One campaign + one phase + resolved targets | `campaign-run` only |

Use **fleet_history** for a global audit trail. Use **campaign_runs** for Campaign UI / operator view: which campaign ran, on which cars, aggregate success/failure, and per-vehicle Docker outcomes.

## Filename pattern

```
run_<YYYYMMDD_HHMMSS>_<campaign_id>_<phase>.json
```

Example:

`run_20260519_054158_campaign_adas_v0.5_car1_canary_candidate.json`

Unsafe characters in `campaign_id` are replaced with `_`.

## Candidate phase example (9-6)

Written after:

```bash
DeploymentMgr campaign-run deploy/campaigns/campaign_adas_v0.5_car1_canary.json --phase candidate
```

```json
{
  "schema_version": 1,
  "run_id": "run_20260519_054158_campaign_adas_v0.5_car1_canary_candidate",
  "campaign_id": "campaign_adas_v0.5_car1_canary",
  "campaign_file": "deploy/campaigns/campaign_adas_v0.5_car1_canary.json",
  "rollout_policy_id": "policy_single_car_candidate_manual_promote",
  "phase": "candidate",
  "status": "success",
  "started_at": "2026-05-19T05:41:58",
  "finished_at": "2026-05-19T05:42:15",
  "feature": "ADAS",
  "target_version": "v0.5",
  "target_role": "candidate",
  "legacy_slot": "B",
  "docker_runtime": {
    "requested": true,
    "mode": "candidate",
    "pull_requested": true,
    "pull_policy": "missing",
    "image": "localhost:5000/sdv/adas-feature:v0.5"
  },
  "resolved_targets": ["car_1"],
  "aggregate": { "total": 1, "success": 1, "failed": 0 },
  "per_vehicle": [
    {
      "car_id": "car_1",
      "result": "success",
      "reason": "",
      "deploy_id": "deploy_20260519_054201_001",
      "state_updated": true,
      "config_synced": true,
      "docker_runtime_result": "success",
      "docker_runtime_mode": "candidate",
      "docker_runtime_action": "deploy-shadow",
      "docker_runtime_image": "localhost:5000/sdv/adas-feature:v0.5",
      "docker_runtime_container": "car_1_agent_adas_shadow",
      "state_file": "deploy/state/car_1.json",
      "config_file": "config/car/car_1.conf",
      "history_path": "deploy/history/fleet_history.jsonl",
      "fleet_deploy_command": "fleet-deploy vehicle car_1 ADAS B v0.5 --sync-config --no-defer-native --docker-runtime=candidate --docker-pull=missing"
    }
  ],
  "warnings": [],
  "failures": []
}
```

### Failed run example

When deploy fails (e.g. `update_mode is not READY`), a run file is still created:

```json
{
  "status": "failed",
  "aggregate": { "total": 1, "success": 0, "failed": 1 },
  "per_vehicle": [
    {
      "car_id": "car_1",
      "result": "failed",
      "reason": "update_mode is not READY",
      "deploy_id": "deploy_20260519_053832_001"
    }
  ],
  "failures": ["car_1: update_mode is not READY"]
}
```

`status` may also be `partial` when some vehicles succeed and others fail (multi-target campaigns).

## Promote phase (9-7)

After a successful **candidate** run, promote with:

```bash
DeploymentMgr campaign-run deploy/campaigns/campaign_adas_v0.5_car1_canary.json --phase promote
```

Equivalent to `fleet-deploy vehicle car_1 ADAS B v0.5 --sync-config --no-defer-native --docker-runtime=promote` (no registry pull).

```json
{
  "phase": "promote",
  "status": "success",
  "target_role": "primary",
  "docker_runtime": {
    "requested": true,
    "mode": "promote",
    "pull_requested": false,
    "pull_policy": "",
    "image": "localhost:5000/sdv/adas-feature:v0.5"
  },
  "per_vehicle": [
    {
      "result": "success",
      "docker_runtime_result": "success",
      "docker_runtime_mode": "promote",
      "docker_runtime_action": "promote",
      "fleet_deploy_command": "fleet-deploy vehicle car_1 ADAS B v0.5 --sync-config --no-defer-native --docker-runtime=promote"
    }
  ]
}
```

Promote fails if the candidate container is missing or OTA safety gate blocks deploy (same as `fleet-deploy`).

## Related

- `deploy/campaigns/` — campaign definitions
- `deploy/schemas/campaign_run.schema.json` — JSON Schema
- `DeploymentMgr campaign-run` — creates files here

## Exit code vs run record

- Deploy failure → `status=failed` in JSON, process exit code `1`, run file still saved.
- Deploy success but run file write error → `[CAMPAIGN_RUN] run_record_error=...`, deploy exit code `0` (operator can retry export later).
