# Fleet deployment campaigns

A **campaign** defines *what* to deploy: feature, target version, package or Docker image metadata, fleet targeting, and which rollout policy governs stage order.

Campaign files are **declarative metadata only** in this phase. They do not execute deployments by themselves.

## Terminology

| Term | Meaning |
|------|---------|
| **Primary** | The production line currently serving traffic (Docker primary container, `deploy/state` primary version fields). |
| **Candidate** | A new version under validation before primary promotion (Docker candidate container, often `*_shadow` in container names). |

Legacy **version_map slots** (internal CLI only): **A ≈ primary line**, **B ≈ candidate line**. User-facing docs and campaign JSON use `target_role: candidate` instead of slot letters. The optional field `legacy_slot` exists only so future tooling can map to today's `fleet-deploy` CLI, which still accepts `A` or `B`.

## What a campaign contains

- **Identity:** `campaign_id`, `name`, `description`, `status`, `created_at`
- **Artifact:** `feature`, `target_version`, `package_id`, `package_path`, `manifest_path`, `docker_image`, `artifact_type`
- **Policy link:** `rollout_policy_id` → file under `deploy/rollout_policies/`
- **Targeting:** `target_selector` (vehicle, group, region, or group_region)
- **Safety:** `safety_requirements` (kill switch, OTA readiness, approvals)
- **Execution hints:** `execution_options`, `docker_runtime` (maps to existing `fleet-deploy` flags when implemented)

## Relationship to `fleet-deploy` (today)

Until `campaign-run` exists, operators run **`DeploymentMgr fleet-deploy`** manually using the same parameters implied by the campaign file. See [campaign_adas_v0.5_car1_canary.json](campaign_adas_v0.5_car1_canary.json) and the mapping table in the 9-2 design notes.

Typical flow for the documented ADAS v0.5 local-registry test:

1. **Candidate phase** — registry pull + candidate container (`--docker-runtime` + `--docker-pull=missing`)
2. **Manual approval** — operator verifies candidate logs and state
3. **Primary promote phase** — separate command (`--docker-runtime=promote`)

## Files in this directory

| File | Purpose |
|------|---------|
| `campaign_adas_v0.5_car1_canary.json` | Example campaign matching the successful 8-2 Docker E2E test on `car_1` |
| `campaign_adas_v0.5_group_canary.json` | **Validate-only:** `target_selector.scope=group` (canary → `car_1`, `car_10`) |
| `campaign_adas_v0.5_region_kr.json` | **Validate-only:** `target_selector.scope=region` (KR region) |

### `target_selector` scopes (9-4)

Resolved by `TargetingResolver` (used by `campaign-validate`; reused by future `campaign-run`):

| scope | Required fields | Example |
|-------|-----------------|---------|
| `vehicle` | `vehicle_ids[]` | `["car_1"]` |
| `group` | `group` | `"canary"` |
| `region` | `region` | `"KR"` |
| `group_region` | `group`, `region` | canary + KR → `car_1` |

Optional filters on `target_selector` (WARN when registry lacks fields): `compatible_models`, `compatible_regions`, `exclude_car_ids`, `require_online`.

## Related directories

- `deploy/rollout_policies/` — stage order, approvals, success criteria
- `deploy/campaign_runs/` — per-run results (future; written by `campaign-run`)
- `deploy/schemas/` — JSON Schema for validation (future `campaign-validate`)

## Status

**9-2:** Example campaign JSON and README.  
**9-3:** `DeploymentMgr campaign-validate` (read-only).  
**9-4:** `TargetingResolver` for `target_selector` → `car_id` list (read-only).  
**9-5:** `campaign-run --phase candidate` (reuses `deployToVehicleWithSummary`).  
**9-6:** `deploy/campaign_runs/run_*.json` per run.  
**9-7:** `campaign-run --phase promote` (Docker promote, no pull).
