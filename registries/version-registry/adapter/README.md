# VersionRegistryAdapter Prototype

## Purpose

This adapter is a standalone, read-only Python prototype for Phase 3-A.

It reads the existing legacy deployment registry files:

- `sdv-ota-manager/platform/deploy/version_map.json`
- `sdv-ota-manager/platform/deploy/package_registry.json`
- `sdv-ota-manager/platform/deploy/packages/*/*/manifest.json`

and exposes a Primary/Candidate-oriented view without changing existing files or runtime behavior.

## Why It Does Not Modify Existing Files

The current demo flow depends on the legacy files and field names. `DeploymentMgr`, `status-server`, dashboard, scripts, self-test, and deployment JSON files still read these paths directly.

For this reason, the adapter only reads current data and translates terminology:

- legacy stable slot -> Primary Version
- legacy candidate slot -> Candidate Version
- package registry / manifest -> deployable package metadata

It does not write `version_map.json`, `package_registry.json`, package manifests, state files, C++ code, Java code, React code, scripts, or CMake files.

## Run

From the repository root:

```bash
python registries/version-registry/adapter/version_registry_adapter.py --root . --summary
```

Optional feature filter:

```bash
python registries/version-registry/adapter/version_registry_adapter.py --root . --summary --feature ADAS
```

## Output Example

```json
{
  "ok": true,
  "features": ["ADAS", "Energy", "Navigation"],
  "versions": {
    "ADAS": {
      "primary_version": "v0.1",
      "candidate_version": "v0.4",
      "deployable_versions": ["v0.1", "v0.4", "v0.5"]
    }
  },
  "warnings": []
}
```

If Primary/Candidate mapping cannot be inferred from the legacy files, the adapter returns `null` values and a note instead of failing.

## Current Limitations

- This is not connected to C++, `DeploymentMgr`, dashboard, `status-server`, or `Signal_I_F`.
- It does not validate live Docker registry availability.
- It does not write or migrate any legacy schema.
- It treats legacy slots as internal compatibility data only.
- It uses defensive parsing and returns empty values when optional fields are missing.

## Phase 3-B Plan

Phase 3-B should add a read-only `DeploymentStateAdapter` prototype that reads:

- `sdv-ota-manager/platform/deploy/state/car_*.json`

and converts `active_*`, `primary_*`, `pending_active_*`, `shadow_*`, `kill_switch`, and `rollback_stack` fields into a stable Primary/Candidate/Shadow vehicle state view.
