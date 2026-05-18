# DeploymentStateAdapter Prototype

## Purpose

This adapter is a standalone, read-only Python prototype for Phase 3-B.

It reads the existing legacy vehicle deployment state files:

- `sdv-ota-manager/platform/deploy/state/car_*.json`
- `sdv-ota-manager/platform/deploy/rollback/`
- `sdv-ota-manager/platform/deploy/history/fleet_history.jsonl`

and exposes a Primary/Candidate/Shadow/KillSwitch/Rollback-oriented view without changing existing files or runtime behavior.

## Why It Does Not Modify Existing State JSON

The current demo flow depends on the existing `deploy/state/car_*.json` shape. `DeploymentMgr`, `status-server`, dashboard, self-test, rollback logic, and scripts still read these files directly.

For this reason, the adapter only reads current state and translates terminology:

- `primary_version` / `active_version` -> Primary Version
- `pending_active_version` -> Candidate Version
- `shadow_*` -> Shadow Mode / Shadow Candidate
- `kill_switch`, `kill_reason` -> Kill Switch status
- `rollback_stack` and rollback metadata files -> Rollback view

It does not write state JSON, C++ code, Java code, React code, scripts, CMake files, or registry files.

## Run

From the repository root:

```bash
python vehicle-plane/hpc/vehicle-deployment-manager/adapter/deployment_state_adapter.py --root . --summary
```

Inspect one vehicle:

```bash
python vehicle-plane/hpc/vehicle-deployment-manager/adapter/deployment_state_adapter.py --root . --vehicle car_1
```

Inspect one vehicle and feature:

```bash
python vehicle-plane/hpc/vehicle-deployment-manager/adapter/deployment_state_adapter.py --root . --vehicle car_1 --feature ADAS
```

## Output Example

```json
{
  "vehicle_id": "car_1",
  "features": {
    "ADAS": {
      "primary_version": "v0.1",
      "candidate_version": "v0.4",
      "candidate_state": "SHADOW_RUNNING",
      "shadow_mode": {
        "enabled": true,
        "version": "v0.4",
        "status": "RUNNING"
      },
      "kill_switch": {
        "enabled": false,
        "reason": null
      },
      "rollback": {
        "available": true,
        "previous_primary": "v0.1"
      }
    }
  }
}
```

Fleet summary example:

```json
{
  "vehicles": 10,
  "features": {
    "ADAS": {
      "primary_versions": {
        "v0.1": 8,
        "v0.4": 2
      },
      "shadow_running": 3,
      "kill_switch_active": 0
    }
  }
}
```

## Current Limitations

- This is not connected to C++, `DeploymentMgr`, dashboard, `status-server`, or `Signal_I_F`.
- It does not validate live Docker containers or vehicle processes.
- It does not write or migrate any legacy schema.
- It treats legacy `active_*` and slot fields as compatibility data only.
- It uses defensive parsing and returns empty values when optional fields are missing.

## Phase 3-C Plan

Phase 3-C can extend the adapter layer in one of two directions:

1. `PackageRegistryAdapter`: enrich vehicle state with package metadata, image URI, image digest, compatibility, and release status.
2. `DeploymentCommandAdapter`: create a dry-run command mapper that converts Primary/Candidate actions into existing `DeploymentMgr`, `status-server`, `ota_vehicle.sh`, or `Signal_I_F` command paths without changing those backends.
