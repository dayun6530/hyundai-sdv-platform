# DeploymentCommandAdapter Dry-run Prototype

## Purpose

This adapter is a standalone Python prototype for Phase 3-D.

It converts Control Plane commands written in Primary/Candidate/Shadow terminology into legacy command previews for:

- `DeploymentMgr`
- `ota_vehicle.sh`
- `Signal_I_F` REST API
- `status-server` API shapes

The adapter only prints JSON previews. It does not execute any command or call any API.

## Why Dry-run Only

The current demo depends on existing CLI/API behavior and state files. Calling real backends from an adapter prototype could unexpectedly change:

- `deploy/state/car_*.json`
- rollback metadata
- Docker containers
- Signal I/F command status
- dashboard-visible fleet state

For this phase, the adapter validates command mapping rules only.

## Why It Does Not Call Existing Backends

Existing backends are still owned by their current modules:

- `sdv-ota-manager/platform/DeploymentManager`
- `sdv-ota-manager/scripts/ota_vehicle.sh`
- `sdv-feature-flag-platform/status-server/server.js`
- `Signal_I_F`

This prototype is intentionally not connected to those implementations. Phase 4 can decide where the adapter should live in the real Control Plane service.

## Run

Deploy Candidate preview:

```bash
python control-plane/feature-deployment-service/deployment-command-api/adapter/deployment_command_adapter.py --root . --action deploy-candidate --vehicle car_1 --feature ADAS --version v0.4
```

Run Shadow preview:

```bash
python control-plane/feature-deployment-service/deployment-command-api/adapter/deployment_command_adapter.py --root . --action run-shadow --vehicle car_1 --feature ADAS --version v0.4
```

Promote Candidate preview:

```bash
python control-plane/feature-deployment-service/deployment-command-api/adapter/deployment_command_adapter.py --root . --action promote-candidate --vehicle car_1 --feature ADAS
```

Rollback Primary preview:

```bash
python control-plane/feature-deployment-service/deployment-command-api/adapter/deployment_command_adapter.py --root . --action rollback-primary --vehicle car_1 --feature ADAS
```

Activate Kill Switch preview:

```bash
python control-plane/feature-deployment-service/deployment-command-api/adapter/deployment_command_adapter.py --root . --action activate-kill-switch --vehicle car_1 --feature ADAS --reason "critical anomaly"
```

Clear Kill Switch preview:

```bash
python control-plane/feature-deployment-service/deployment-command-api/adapter/deployment_command_adapter.py --root . --action clear-kill-switch --vehicle car_1 --feature ADAS
```

## Output Example

```json
{
  "dry_run": true,
  "action": "run-shadow",
  "vehicle_id": "car_1",
  "feature": "ADAS",
  "version": "v0.4",
  "primary_candidate_command": {
    "semantic_action": "RUN_CANDIDATE_AS_SHADOW",
    "description": "Run ADAS v0.4 as Shadow Candidate on car_1 without changing Primary"
  },
  "legacy_command_preview": [
    {
      "type": "DeploymentMgr",
      "command": "cd sdv-ota-manager/platform && ./DeploymentManager/build/DeploymentMgr fleet-shadow vehicle car_1 ADAS ON"
    },
    {
      "type": "ota_vehicle.sh",
      "command": "sdv-ota-manager/scripts/ota_vehicle.sh deploy-shadow --car car_1 --domain ADAS --version v0.4"
    }
  ],
  "will_execute": false
}
```

## Action Mapping

| Action | Meaning | Legacy preview target |
|---|---|---|
| `deploy-candidate` | Prepare Candidate Version for a vehicle/group/region | `DeploymentMgr fleet-deploy ...`, `/api/fleet/deploy` body |
| `run-shadow` | Run Candidate as Shadow without changing Primary | `DeploymentMgr fleet-shadow ...`, `ota_vehicle.sh deploy-shadow ...` |
| `promote-candidate` | Promote validated Candidate to Primary | `DeploymentMgr fleet-promote-shadow ...`, `ota_vehicle.sh promote ...` |
| `rollback-primary` | Roll back to previous Primary | `DeploymentMgr fleet-rollback ...` |
| `activate-kill-switch` | Block/fallback feature due to risk | `DeploymentMgr fleet-kill ...`, `Signal_I_F POST /api/v1/control/kill-switch` |
| `clear-kill-switch` | Clear Kill Switch | TODO endpoint preview only |

## Current Limitations

- No command execution.
- No HTTP calls.
- No validation against live `version_map.json`, package registry, or vehicle state.
- No automatic Candidate Version resolution for promote.
- `clear-kill-switch` endpoint is intentionally marked TODO until current backend behavior is confirmed.

## Phase 4 Pre-connection Checklist

Before wiring this adapter into a real Control Plane API:

1. Confirm exact `DeploymentMgr` command syntax to use as the stable backend contract.
2. Confirm whether Shadow runtime should use `DeploymentMgr`, `ota_vehicle.sh`, or both.
3. Confirm clear Kill Switch semantics and endpoint.
4. Decide whether `reason` must be normalized to a single token for legacy CLI.
5. Add tests with expected JSON previews.
6. Only then connect through `status-server` or a new Control Plane service.
