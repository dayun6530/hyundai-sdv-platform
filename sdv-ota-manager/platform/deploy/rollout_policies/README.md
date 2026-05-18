# Rollout policies

A **rollout policy** defines *how* a campaign is rolled out: stage order, approvals, Docker runtime expectations, health checks, and failure handling.

Policies are **reference and planning artifacts** in 9-2. The DeploymentManager does not execute stages automatically yet.

## Typical progressive flow

```text
candidate deploy  →  manual approval  →  primary promote
     (Docker)            (operator)         (Docker)
```

1. **Candidate deploy** — Update fleet metadata, pull image if needed, start the **candidate** container. Primary containers keep serving the previous version until promote.
2. **Manual approval** — Operator checks `docker logs`, `deploy/state`, and `deploy/vehicle_runtime` before allowing promote.
3. **Primary promote** — Run Docker promote so the validated image becomes the **primary** container; candidate lane is cleared.

## Docker-related checks

Policies may require:

- `docker_runtime_result: success` in `deploy/state` after each Docker phase
- **Log patterns** in candidate or primary containers (e.g. `[ADAS v0.5]`, `Candidate Shadow version is running`)
- **Safety:** `forbid_kill_switch`, `require_safe_update_mode` (aligned with OTA READY / parked install gates used by `fleet-deploy`)
- **Registry pull:** `docker_runtime.pull_policy: missing` (pull only when the image is not local)

## Files in this directory

| File | Purpose |
|------|---------|
| `policy_single_car_candidate_manual_promote.json` | Single-vehicle policy used by `campaign_adas_v0.5_car1_canary` |
| `policy_canary_then_stable.json` | **Reference only** — canary → beta → stable expansion template; not wired to automation |

## Multi-stage expansion (reference)

`policy_canary_then_stable.json` shows how later campaigns might:

- Override targeting per stage (`target_selector_override` with `group: canary` | `beta` | `stable`)
- Use `percentage` for fleet fraction (documentation field until targeting resolver implements it)
- Require manual approval between stages
- Set `rollback_on_failure` per stage

No executor reads this file in 9-2.

## Related

- `deploy/campaigns/` — links via `rollout_policy_id`
- `deploy/schemas/rollout_policy.schema.json` — validation shape (for future `campaign-validate`)
