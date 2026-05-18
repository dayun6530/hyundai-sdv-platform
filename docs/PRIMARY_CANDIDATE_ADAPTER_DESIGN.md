# Primary/Candidate Adapter Design

Phase: 2 - Primary/Candidate Adapter Design

This document is a design-only artifact. It does not move, rename, delete, or modify existing runtime code, build files, scripts, dashboard code, `status-server`, `DeploymentManager`, `Signal_I_F`, or existing deployment JSON schemas.

## 1. Why Adapters Are Needed

The current deployment implementation is working and demo-ready, but it still exposes legacy terms such as `slot`, `A`, `B`, `active_slot`, `fallback_slot`, and `pending_active_slot` through C++ state models, JSON files, status-server APIs, and dashboard UI.

The target architecture should use operational SDV/OTA language:

- Primary Version: the version currently used for real vehicle control.
- Candidate Version: the newly received version being evaluated or staged.
- Shadow Mode: Candidate Version runs behind Primary without controlling the vehicle.
- A/B Rollout: a fleet strategy where different vehicle groups run different Primary Versions.
- Canary: a small initial rollout group.
- Rollback: return to the previous Primary Version.
- Kill Switch: immediately block or fall back a feature during risk.

Adapters let the project introduce these terms without breaking the current demo. They translate between the stable legacy storage/API shape and the future Primary/Candidate model.

## 2. Problems With the Legacy A/B/Slot Structure

The legacy structure is useful for the current demo, but it creates ambiguity:

- `A` and `B` can mean stable/candidate in `version_map.json`, but A/B Rollout in the target architecture means a fleet strategy, not a local slot.
- `active_*` and `primary_*` coexist in `deploy/state/car_*.json`.
- `pending_active_*` is a Candidate-like field, but it is not the same as Shadow.
- `shadow_*` fields represent Candidate validation, but current UI/API still often speaks in slot terms.
- Control Plane code in `status-server/server.js` and dashboard components currently reads legacy structures directly.

The adapters should keep existing fields intact while presenting a stable conceptual model to new code and documentation.

## 3. Primary/Candidate/Shadow Domain Model

Conceptual model exposed by adapters:

```ts
type FeatureName = "ADAS" | "Navigation" | "Energy";

interface VersionRef {
  feature: FeatureName;
  version: string;
  role: "primary" | "candidate" | "shadow" | "rollback" | "unknown";
  legacySlot?: "A" | "B" | "";
  binary?: string;
  dockerImage?: string;
  imageDigest?: string;
  packageId?: string;
}

interface ShadowModeView {
  enabled: boolean;
  version: string | null;
  status: string | null;
  validationResult?: string | null;
}

interface KillSwitchView {
  enabled: boolean;
  reason: string | null;
}

interface PrimaryCandidateVehicleView {
  vehicle_id: string;
  feature: FeatureName;
  primary_version: string | null;
  candidate_version: string | null;
  candidate_state: "NONE" | "PENDING" | "SHADOW_RUNNING" | "SHADOW_DISABLED" | "PROMOTED" | "FAILED";
  shadow_mode: ShadowModeView;
  kill_switch: KillSwitchView;
  rollback_stack: VersionRef[];
}
```

## 4. VersionRegistryAdapter Design

### Purpose

`VersionRegistryAdapter` presents `version_map.json` and `package_registry.json` as a future Version Registry without changing either file.

### Current Implementation Mapping

- `sdv-ota-manager/platform/deploy/version_map.json`
- `sdv-ota-manager/platform/deploy/package_registry.json`
- `sdv-ota-manager/platform/DeploymentManager/src/VersionManager.cpp`
- `sdv-ota-manager/platform/DeploymentManager/include/DeploymentManager/VersionManager.h`
- `sdv-ota-manager/scripts/update_version_map.py`

### Conceptual API Draft

```ts
interface VersionRegistryAdapter {
  getPrimaryVersion(feature: FeatureName): VersionRef | null;
  getCandidateVersion(feature: FeatureName): VersionRef | null;
  getPackageMetadata(feature: FeatureName, version: string): PackageMetadata | null;
  listDeployableVersions(feature: FeatureName): VersionRef[];
  resolveImageUri(feature: FeatureName, version: string): string | null;
  isVersionDeployable(feature: FeatureName, version: string): DeployabilityResult;
  mapLegacySlotToPrimaryCandidate(feature: FeatureName): LegacySlotMapping;
}

interface LegacySlotMapping {
  feature: FeatureName;
  primary: VersionRef | null;
  candidate: VersionRef | null;
  legacy: {
    stableSlot: "A" | "B" | "";
    candidateSlot: "A" | "B" | "";
  };
}
```

### Mapping Strategy

- `role: "stable"` in `version_map.json` maps to Primary Version.
- `role: "candidate"` in `version_map.json` maps to Candidate Version.
- Legacy slot keys remain internal metadata only.
- Package metadata is enriched from `package_registry.json` and `deploy/packages/<FEATURE>/<version>/manifest.json`.

### Deployability Rules

`isVersionDeployable(feature, version)` should check:

- Feature exists in the registry.
- Version exists in package registry or legacy version map.
- Package manifest exists when package metadata indicates Docker deployment.
- Docker image URI is present for Docker image packages.
- `checksum_file` or `image_digest` exists.
- Compatible model/region fields can be evaluated later against `VehicleRegistry`.

## 5. DeploymentStateAdapter Design

### Purpose

`DeploymentStateAdapter` reads `deploy/state/car_*.json` and converts legacy state into a stable Primary/Candidate/Shadow view.

### Current Implementation Mapping

- `sdv-ota-manager/platform/deploy/state/car_*.json`
- `sdv-ota-manager/platform/DeploymentManager/src/StateManager.cpp`
- `sdv-ota-manager/platform/DeploymentManager/include/DeploymentManager/StateManager.h`

### Conceptual API Draft

```ts
interface DeploymentStateAdapter {
  getVehiclePrimary(vehicleId: string, feature: FeatureName): VersionRef | null;
  getVehicleCandidate(vehicleId: string, feature: FeatureName): VersionRef | null;
  getShadowStatus(vehicleId: string, feature: FeatureName): ShadowModeView;
  getKillSwitchStatus(vehicleId: string, feature: FeatureName): KillSwitchView;
  getRollbackStack(vehicleId: string, feature: FeatureName): VersionRef[];
  toPrimaryCandidateView(rawState: unknown): PrimaryCandidateVehicleView[];
  fromPrimaryCandidateCommand(command: PrimaryCandidateCommand): LegacyDeploymentCommand;
}
```

### Field Interpretation

| Legacy field | Adapter meaning |
|---|---|
| `primary_version` | Preferred Primary Version source |
| `active_version` | Legacy fallback for Primary Version |
| `pending_active_version` | Candidate Version staged for later activation |
| `shadow_version` | Candidate Version currently assigned to Shadow Mode |
| `shadow_enabled` | Shadow Mode enabled flag |
| `shadow_status` | Candidate validation/runtime status |
| `kill_switch` | Kill Switch enabled flag |
| `kill_reason` | Kill Switch reason |
| `rollback_stack` | Previous Primary Versions available for rollback |

### Candidate Resolution Rule

Candidate Version should be resolved in this order:

1. If `shadow_enabled === true` and `shadow_version` is present, Candidate state is `SHADOW_RUNNING`.
2. Else if `pending_active_version` is present, Candidate state is `PENDING`.
3. Else Candidate state is `NONE`.

## 6. PackageRegistryAdapter Design

### Purpose

`PackageRegistryAdapter` provides package and Docker metadata independent of the current file layout.

### Current Implementation Mapping

- `sdv-ota-manager/platform/deploy/package_registry.json`
- `sdv-ota-manager/platform/deploy/packages/*/*/manifest.json`
- `sdv-ota-manager/platform/deploy/package_registry.schema.json`
- `sdv-ota-manager/platform/deploy/manifest.schema.json`

### Conceptual API Draft

```ts
interface PackageRegistryAdapter {
  listPackages(): PackageMetadata[];
  findPackage(feature: FeatureName, version: string): PackageMetadata | null;
  validatePackageForVehicle(feature: FeatureName, version: string, vehicleId: string): PackageValidationResult;
  getDockerImage(feature: FeatureName, version: string): string | null;
  getImageDigest(feature: FeatureName, version: string): string | null;
}

interface PackageMetadata {
  packageId: string;
  feature: FeatureName;
  version: string;
  artifactType: "docker_image" | "executable" | "unknown";
  dockerImage?: string;
  imageDigest?: string;
  checksumFile?: string;
  compatibleModels: string[];
  compatibleRegions: string[];
  releaseNote?: string;
  status?: string;
  manifestPath?: string;
}
```

### Future Extension

The adapter should later switch its backend from local JSON files to:

- Docker Registry API
- Version Registry service
- Package provenance/attestation store
- CI/CD release metadata

## 7. DeploymentCommandAdapter Design

### Purpose

`DeploymentCommandAdapter` translates Control Plane commands written in Primary/Candidate language into existing `DeploymentMgr`, status-server, Signal I/F, or `ota_vehicle.sh` calls.

It must not change existing CLI commands in Phase 2.

### Current Implementation Mapping

- `sdv-feature-flag-platform/status-server/server.js` `/api/fleet/deploy`
- `sdv-feature-flag-platform/status-server/server.js` `/api/fleet/action`
- `sdv-ota-manager/platform/DeploymentManager/src/DeploymentManagerMain.cpp`
- `sdv-ota-manager/scripts/ota_vehicle.sh`
- `Signal_I_F` deploy/shadow/kill REST and MQTT flows

### Conceptual API Draft

```ts
interface DeploymentCommandAdapter {
  deployCandidate(vehicleId: string, feature: FeatureName, version: string): LegacyCommand;
  runShadow(vehicleId: string, feature: FeatureName, version: string): LegacyCommand;
  promoteCandidateToPrimary(vehicleId: string, feature: FeatureName): LegacyCommand;
  rollbackToPreviousPrimary(vehicleId: string, feature: FeatureName): LegacyCommand;
  activateKillSwitch(vehicleId: string, feature: FeatureName, reason: string): LegacyCommand;
  clearKillSwitch(vehicleId: string, feature: FeatureName): LegacyCommand;
}
```

### Legacy Command Mapping

| New command | Existing backend mapping |
|---|---|
| `deployCandidate(vehicleId, feature, version)` | `DeploymentMgr fleet-deploy vehicle <car_id> <FEATURE> <legacyCandidateSlot> <VERSION>` |
| `runShadow(vehicleId, feature, version)` | `DeploymentMgr fleet-shadow vehicle <car_id> <FEATURE> ON` and/or `ota_vehicle.sh deploy-shadow` |
| `promoteCandidateToPrimary(vehicleId, feature)` | `DeploymentMgr fleet-promote-shadow vehicle <car_id> <FEATURE>` and/or `ota_vehicle.sh promote` |
| `rollbackToPreviousPrimary(vehicleId, feature)` | `DeploymentMgr fleet-rollback vehicle <car_id> <FEATURE>` |
| `activateKillSwitch(vehicleId, feature, reason)` | `DeploymentMgr fleet-kill vehicle <car_id> <FEATURE> <reason>` and/or Signal I/F kill API |
| `clearKillSwitch(vehicleId, feature)` | Planned. Existing explicit clear path must be designed before implementation. |

## 8. DashboardLabelAdapter Design

### Purpose

`DashboardLabelAdapter` lets the UI show Primary/Candidate/Shadow terminology without immediately changing API response shape or JSON fields.

### Label Mapping

| Legacy label/field | UI label |
|---|---|
| `active_slot` | Primary Runtime Slot (legacy/internal) |
| `active_version` | Primary Version |
| `primary_version` | Primary Version |
| `pending_active_version` | Candidate Version |
| `shadow_version` | Shadow Candidate Version |
| `shadow_enabled` | Shadow Mode Enabled |
| `slot A/B` | Legacy Slot (internal compatibility) |
| `fallback_slot` | Rollback/Fallback Slot (legacy/internal) |

### Strategy

- Phase 2: document mapping only.
- Phase 3: add a pure mapping helper in dashboard code after approval.
- Phase 4: update UI labels while preserving raw details in an advanced/legacy section.

## 9. Existing Files to Adapter Mapping

| Existing file/path | Adapter |
|---|---|
| `sdv-ota-manager/platform/deploy/version_map.json` | `VersionRegistryAdapter` |
| `sdv-ota-manager/platform/deploy/package_registry.json` | `VersionRegistryAdapter`, `PackageRegistryAdapter` |
| `sdv-ota-manager/platform/deploy/packages/*/*/manifest.json` | `PackageRegistryAdapter` |
| `sdv-ota-manager/platform/deploy/state/car_*.json` | `DeploymentStateAdapter` |
| `sdv-ota-manager/platform/DeploymentManager/src/VersionManager.cpp` | future backend for `VersionRegistryAdapter` |
| `sdv-ota-manager/platform/DeploymentManager/src/StateManager.cpp` | future backend for `DeploymentStateAdapter` |
| `sdv-ota-manager/platform/DeploymentManager/src/DeploymentManager.cpp` | future backend for `DeploymentCommandAdapter` |
| `sdv-feature-flag-platform/status-server/server.js` | future Control Plane host for adapters |
| `sdv-feature-flag-platform/dashboard/src/fleet/*` | future consumer of `DashboardLabelAdapter` |
| `sdv-ota-manager/scripts/ota_vehicle.sh` | runtime backend for Shadow/Promote command adapter |
| `Signal_I_F/` | Signal command backend for Kill/Shadow/Deploy paths |

## 10. JSON Conversion Examples

### Example 1. Existing Raw State

```json
{
  "vehicle_id": "car_1",
  "active_ADAS": "v0.1",
  "primary_ADAS": "v0.1",
  "pending_active_ADAS": "v0.2",
  "shadow_enabled": true,
  "shadow_version": "v0.2",
  "shadow_status": "RUNNING",
  "kill_switch": false
}
```

### Example 2. Primary/Candidate View

```json
{
  "vehicle_id": "car_1",
  "feature": "ADAS",
  "primary_version": "v0.1",
  "candidate_version": "v0.2",
  "candidate_state": "SHADOW_RUNNING",
  "shadow_mode": {
    "enabled": true,
    "version": "v0.2",
    "status": "RUNNING"
  },
  "kill_switch": {
    "enabled": false,
    "reason": null
  }
}
```

### Conversion Notes

- `primary_version` prefers `primary_ADAS`, then `active_ADAS`.
- `candidate_version` prefers `shadow_version` when Shadow Mode is enabled, then `pending_active_ADAS`.
- `candidate_state` is derived, not stored.
- `kill_switch.reason` is `null` when no reason exists.

## 11. Phase 3 Implementation Order

1. Add read-only adapter interfaces as non-build documentation or TypeScript/C++ header drafts, depending on approved target language.
2. Implement a pure JSON transformation prototype outside existing C++ build targets.
3. Add tests using copied fixture JSON, not live `deploy/state/car_*.json`.
4. Add `VersionRegistryAdapter` read-only implementation.
5. Add `DeploymentStateAdapter` read-only implementation.
6. Add `PackageRegistryAdapter` read-only implementation.
7. Add `DashboardLabelAdapter` helper and update labels only after UI approval.
8. Add `DeploymentCommandAdapter` as a wrapper around existing CLI/API calls.
9. Only after all demos pass, consider replacing direct status-server reads with adapter calls.

## 12. Do Not Modify in Phase 2

- `sdv-ota-manager/platform/DeploymentManager/src/DeploymentManager.cpp`
- `sdv-ota-manager/platform/DeploymentManager/src/StateManager.cpp`
- `sdv-ota-manager/platform/DeploymentManager/include/DeploymentManager/StateManager.h`
- `sdv-ota-manager/platform/DeploymentManager/src/VersionManager.cpp`
- `sdv-ota-manager/platform/DeploymentManager/include/DeploymentManager/VersionManager.h`
- `sdv-ota-manager/platform/DeploymentManager/src/DeploymentManagerMain.cpp`
- `sdv-ota-manager/platform/deploy/state/car_*.json`
- `sdv-ota-manager/platform/deploy/version_map.json`
- `sdv-ota-manager/platform/deploy/package_registry.json`
- `sdv-feature-flag-platform/status-server/server.js`
- `sdv-feature-flag-platform/dashboard/src/fleet/*`
- `sdv-feature-flag-platform/dashboard/src/api/fleet.js`
- `sdv-feature-flag-platform/dashboard/src/api/vehicleRuntime.js`
- `sdv-ota-manager/scripts/ota_vehicle.sh`
- `sdv-ota-manager/scripts/build_package_image.sh`
- `Signal_I_F/src/main/java/com/sdv/killcontrol/sdv/*`

## 13. Phase 2 Decision Summary

The adapter layer is a compatibility boundary. It should introduce the final architecture vocabulary while preserving current storage, APIs, scripts, CLI commands, and demo behavior.
