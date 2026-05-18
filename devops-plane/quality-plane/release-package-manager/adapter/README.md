# PackageRegistryAdapter Prototype

## Purpose

This adapter is a standalone, read-only Python prototype for Phase 3-C.

It reads existing package metadata:

- `sdv-ota-manager/platform/deploy/package_registry.json`
- `sdv-ota-manager/platform/deploy/packages/*/*/manifest.json`
- `sdv-ota-manager/platform/config/fleet/vehicles.json` for optional vehicle compatibility checks

and exposes a Feature / Version / Docker Image / Digest / Compatibility / Deployable Status view without changing existing files or runtime behavior.

## Why It Does Not Modify Existing Package Files

The current demo flow depends on the existing package registry and manifest layout. `DeploymentMgr`, `status-server`, dashboard, scripts, and self-test still read these files directly.

For this reason, the adapter only reads current package data and translates it into a safer view:

- Feature and Version
- Docker image URI
- image digest and checksum metadata
- compatible models and regions
- deployability status
- vehicle compatibility result

It does not write `package_registry.json`, manifests, `vehicles.json`, state JSON, C++ code, Java code, React code, scripts, or CMake files.

## Run

From the repository root:

```bash
python devops-plane/quality-plane/release-package-manager/adapter/package_registry_adapter.py --root . --summary
```

List packages for one feature:

```bash
python devops-plane/quality-plane/release-package-manager/adapter/package_registry_adapter.py --root . --feature ADAS
```

Inspect one package:

```bash
python devops-plane/quality-plane/release-package-manager/adapter/package_registry_adapter.py --root . --feature ADAS --version v0.4
```

Check package compatibility for one vehicle:

```bash
python devops-plane/quality-plane/release-package-manager/adapter/package_registry_adapter.py --root . --feature ADAS --version v0.4 --vehicle car_1
```

## Output Example

```json
{
  "feature": "ADAS",
  "version": "v0.4",
  "docker_image": "sdv/adas-feature:v0.4",
  "image_digest": "sha256:...",
  "status": "READY",
  "compatible_models": ["IONIQ5", "IONIQ6"],
  "compatible_regions": ["KR"],
  "deployable": true
}
```

Vehicle compatibility example:

```json
{
  "vehicle_id": "car_1",
  "feature": "ADAS",
  "version": "v0.4",
  "compatible": true,
  "reasons": []
}
```

If package or vehicle metadata is incomplete, the adapter returns `null` or `false` with reasons instead of failing.

## Current Limitations

- This is not connected to C++, `DeploymentMgr`, dashboard, `status-server`, or `Signal_I_F`.
- It does not query a live Docker Registry.
- It does not verify real image pull availability.
- It does not write or migrate any package schema.
- It uses defensive parsing and returns empty values when optional fields are missing.

## Phase 3-D Plan

Phase 3-D should add a read-only/dry-run `DeploymentCommandAdapter` prototype that converts Primary/Candidate actions into existing backend commands without executing them:

- `deployCandidate(...)` -> `DeploymentMgr fleet-deploy ...`
- `runShadow(...)` -> `DeploymentMgr fleet-shadow ...` or `ota_vehicle.sh deploy-shadow ...`
- `promoteCandidateToPrimary(...)` -> `DeploymentMgr fleet-promote-shadow ...` or `ota_vehicle.sh promote ...`
- `rollbackToPreviousPrimary(...)` -> `DeploymentMgr fleet-rollback ...`
- `activateKillSwitch(...)` -> `DeploymentMgr fleet-kill ...` or Signal I/F kill API
