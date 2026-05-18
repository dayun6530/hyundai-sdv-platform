#!/usr/bin/env python3
"""Dry-run DeploymentCommandAdapter prototype.

This Phase 3-D adapter converts Primary/Candidate/Shadow command vocabulary into
legacy command previews without executing DeploymentMgr, ota_vehicle.sh, Signal_I_F,
or any other runtime backend.
"""

from __future__ import annotations

import argparse
import json
import shlex
from pathlib import Path
from typing import Any, Dict, List, Optional


JsonObject = Dict[str, Any]

SUPPORTED_ACTIONS = [
    "deploy-candidate",
    "run-shadow",
    "promote-candidate",
    "rollback-primary",
    "activate-kill-switch",
    "clear-kill-switch",
]

VALID_FEATURES = {"ADAS", "Navigation", "Energy"}


def _normalize_feature(value: str) -> str:
    raw = str(value or "").strip()
    compact = raw.lower().replace("_", "").replace("-", "").replace(" ", "")
    if compact in {"adas", "adasfeature"}:
        return "ADAS"
    if compact in {"navigation", "nav", "navigationfeature"}:
        return "Navigation"
    if compact in {"energy", "evenergy", "ev", "evenergyfeature"}:
        return "Energy"
    return raw


def _quote(value: str) -> str:
    return shlex.quote(str(value))


def _error(message: str, action: Optional[str] = None) -> JsonObject:
    return {
        "ok": False,
        "dry_run": True,
        "action": action,
        "error": message,
        "available_actions": SUPPORTED_ACTIONS,
        "will_execute": False,
    }


class DeploymentCommandAdapter:
    """Builds dry-run previews for legacy deployment command paths."""

    def __init__(self, root: str | Path = ".") -> None:
        self.root = Path(root).resolve()
        self.platform_root = self.root / "sdv-ota-manager" / "platform"
        self.deployment_mgr = "./DeploymentManager/build/DeploymentMgr"
        self.ota_vehicle = "sdv-ota-manager/scripts/ota_vehicle.sh"
        self.signal_if_base = "Signal_I_F /api/v1"

    def preview(
        self,
        action: str,
        vehicle: str = "",
        feature: str = "",
        version: str = "",
        reason: str = "",
        group: str = "",
        region: str = "",
    ) -> JsonObject:
        normalized_action = str(action or "").strip()
        if normalized_action not in SUPPORTED_ACTIONS:
            return _error(f"Unsupported action: {normalized_action or '(empty)'}", normalized_action)

        feature_name = _normalize_feature(feature)
        validation_error = self._validate(normalized_action, vehicle, feature_name, version, reason, group, region)
        if validation_error:
            return _error(validation_error, normalized_action)

        builders = {
            "deploy-candidate": self._deploy_candidate,
            "run-shadow": self._run_shadow,
            "promote-candidate": self._promote_candidate,
            "rollback-primary": self._rollback_primary,
            "activate-kill-switch": self._activate_kill_switch,
            "clear-kill-switch": self._clear_kill_switch,
        }
        return builders[normalized_action](vehicle, feature_name, version, reason, group, region)

    def _validate(
        self,
        action: str,
        vehicle: str,
        feature: str,
        version: str,
        reason: str,
        group: str,
        region: str,
    ) -> Optional[str]:
        if not feature:
            return "--feature is required"
        if feature not in VALID_FEATURES:
            return f"--feature must be one of {sorted(VALID_FEATURES)}"

        if action in {"deploy-candidate", "run-shadow"} and not version:
            return f"--version is required for action {action}"

        if action == "activate-kill-switch" and not reason:
            return "--reason is required for action activate-kill-switch"

        if action == "deploy-candidate":
            if not (vehicle or group or region):
                return "--vehicle, --group, or --region is required for deploy-candidate"
            return None

        if not vehicle:
            return f"--vehicle is required for action {action}"
        return None

    def _base_payload(
        self,
        action: str,
        vehicle: str,
        feature: str,
        version: str,
        semantic_action: str,
        description: str,
    ) -> JsonObject:
        return {
            "ok": True,
            "dry_run": True,
            "action": action,
            "vehicle_id": vehicle or None,
            "feature": feature,
            "version": version or None,
            "root": self.root.as_posix(),
            "primary_candidate_command": {
                "semantic_action": semantic_action,
                "description": description,
            },
            "legacy_command_preview": [],
            "will_execute": False,
            "notes": [
                "This is a dry-run preview only.",
                "No existing state JSON, runtime container, DeploymentMgr command, script, or Signal_I_F API is executed.",
            ],
        }

    def _deploy_candidate(self, vehicle: str, feature: str, version: str, reason: str, group: str, region: str) -> JsonObject:
        del reason
        target_desc = self._target_description(vehicle, group, region)
        payload = self._base_payload(
            "deploy-candidate",
            vehicle,
            feature,
            version,
            "DEPLOY_CANDIDATE",
            f"Prepare {feature} {version} as Candidate Version for {target_desc}",
        )
        scope, target_args = self._fleet_deploy_scope(vehicle, group, region)
        payload["target"] = {"scope": scope, "vehicle": vehicle or None, "group": group or None, "region": region or None}
        payload["legacy_command_preview"] = [
            {
                "type": "DeploymentMgr",
                "command": (
                    f"cd sdv-ota-manager/platform && {self.deployment_mgr} "
                    f"fleet-deploy {scope} {target_args} {_quote(feature)} B {_quote(version)}"
                ),
                "note": "Legacy candidate slot is represented as B internally; adapter keeps this as compatibility detail.",
            },
            {
                "type": "status-server API",
                "method": "POST",
                "endpoint": "/api/fleet/deploy",
                "body": {
                    "scope": scope,
                    "feature": feature,
                    "slot": "B",
                    "version": version,
                    **({"car_id": vehicle} if vehicle else {}),
                    **({"group": group} if group else {}),
                    **({"region": region} if region else {}),
                },
                "note": "Preview only; status-server is not called.",
            },
        ]
        return payload

    def _run_shadow(self, vehicle: str, feature: str, version: str, reason: str, group: str, region: str) -> JsonObject:
        del reason, group, region
        payload = self._base_payload(
            "run-shadow",
            vehicle,
            feature,
            version,
            "RUN_CANDIDATE_AS_SHADOW",
            f"Run {feature} {version} as Shadow Candidate on {vehicle} without changing Primary",
        )
        payload["legacy_command_preview"] = [
            {
                "type": "DeploymentMgr",
                "command": f"cd sdv-ota-manager/platform && {self.deployment_mgr} fleet-shadow vehicle {_quote(vehicle)} {_quote(feature)} ON",
                "note": "Current legacy CLI resolves shadow version from the candidate slot in version_map.json; requested version is retained in adapter context.",
            },
            {
                "type": "ota_vehicle.sh",
                "command": f"{self.ota_vehicle} deploy-shadow --car {_quote(vehicle)} --domain {_quote(feature)} --version {_quote(version)}",
                "note": "Preview only; Docker containers are not touched.",
            },
        ]
        return payload

    def _promote_candidate(self, vehicle: str, feature: str, version: str, reason: str, group: str, region: str) -> JsonObject:
        del version, reason, group, region
        payload = self._base_payload(
            "promote-candidate",
            vehicle,
            feature,
            "",
            "PROMOTE_CANDIDATE_TO_PRIMARY",
            f"Promote validated Shadow Candidate for {feature} on {vehicle} to Primary Version",
        )
        payload["legacy_command_preview"] = [
            {
                "type": "DeploymentMgr",
                "command": f"cd sdv-ota-manager/platform && {self.deployment_mgr} fleet-promote-shadow vehicle {_quote(vehicle)} {_quote(feature)}",
            },
            {
                "type": "ota_vehicle.sh",
                "command": f"{self.ota_vehicle} promote --car {_quote(vehicle)} --domain {_quote(feature)} --version <candidate-version>",
                "note": "Candidate version must be resolved from state/adapter before real execution.",
            },
        ]
        return payload

    def _rollback_primary(self, vehicle: str, feature: str, version: str, reason: str, group: str, region: str) -> JsonObject:
        del version, reason, group, region
        payload = self._base_payload(
            "rollback-primary",
            vehicle,
            feature,
            "",
            "ROLLBACK_TO_PREVIOUS_PRIMARY",
            f"Rollback {feature} on {vehicle} to the previous Primary Version",
        )
        payload["legacy_command_preview"] = [
            {
                "type": "DeploymentMgr",
                "command": f"cd sdv-ota-manager/platform && {self.deployment_mgr} fleet-rollback vehicle {_quote(vehicle)} {_quote(feature)}",
            }
        ]
        return payload

    def _activate_kill_switch(self, vehicle: str, feature: str, version: str, reason: str, group: str, region: str) -> JsonObject:
        del version, group, region
        payload = self._base_payload(
            "activate-kill-switch",
            vehicle,
            feature,
            "",
            "ACTIVATE_KILL_SWITCH",
            f"Block or fall back {feature} on {vehicle} because of safety reason: {reason}",
        )
        payload["reason"] = reason
        payload["legacy_command_preview"] = [
            {
                "type": "DeploymentMgr",
                "command": f"cd sdv-ota-manager/platform && {self.deployment_mgr} fleet-kill vehicle {_quote(vehicle)} {_quote(feature)} {_quote(reason)}",
                "note": "Legacy CLI currently expects reason as a single token; adapter preview preserves the provided reason for review.",
            },
            {
                "type": "Signal_I_F API",
                "method": "POST",
                "endpoint": "/api/v1/control/kill-switch",
                "body": {
                    "vehicleId": vehicle,
                    "targetFeature": feature,
                },
                "note": "Preview only; Signal_I_F API is not called.",
            },
        ]
        return payload

    def _clear_kill_switch(self, vehicle: str, feature: str, version: str, reason: str, group: str, region: str) -> JsonObject:
        del version, reason, group, region
        payload = self._base_payload(
            "clear-kill-switch",
            vehicle,
            feature,
            "",
            "CLEAR_KILL_SWITCH",
            f"Clear Kill Switch state for {feature} on {vehicle}",
        )
        payload["legacy_command_preview"] = [
            {
                "type": "Signal_I_F API",
                "method": "POST or DELETE",
                "endpoint": "TODO: confirm clear kill switch endpoint",
                "body": {
                    "vehicleId": vehicle,
                    "targetFeature": feature,
                    "clear": True,
                },
                "note": "No confirmed clear endpoint exists in the current adapter scope.",
            }
        ]
        payload["notes"].append("TODO: confirm whether clear kill switch is a DeploymentMgr command, Signal_I_F endpoint, or both.")
        return payload

    def _fleet_deploy_scope(self, vehicle: str, group: str, region: str) -> tuple[str, str]:
        if vehicle:
            return "vehicle", _quote(vehicle)
        if group and region:
            return "group_region", f"{_quote(group)} {_quote(region)}"
        if group:
            return "group", _quote(group)
        return "region", _quote(region)

    def _target_description(self, vehicle: str, group: str, region: str) -> str:
        if vehicle:
            return vehicle
        if group and region:
            return f"group={group}, region={region}"
        if group:
            return f"group={group}"
        if region:
            return f"region={region}"
        return "unknown target"


def main() -> int:
    parser = argparse.ArgumentParser(description="Dry-run deployment command adapter.")
    parser.add_argument("--root", default=".", help="Repository root path (default: current directory)")
    parser.add_argument("--action", required=True, help=f"Action: {', '.join(SUPPORTED_ACTIONS)}")
    parser.add_argument("--vehicle", default="", help="Vehicle id, e.g. car_1")
    parser.add_argument("--group", default="", help="Fleet group for deploy-candidate, e.g. canary")
    parser.add_argument("--region", default="", help="Fleet region for deploy-candidate, e.g. KR")
    parser.add_argument("--feature", default="", help="Feature, e.g. ADAS")
    parser.add_argument("--version", default="", help="Candidate version, e.g. v0.4")
    parser.add_argument("--reason", default="", help="Kill switch reason")
    args = parser.parse_args()

    adapter = DeploymentCommandAdapter(args.root)
    payload = adapter.preview(
        action=args.action,
        vehicle=args.vehicle,
        feature=args.feature,
        version=args.version,
        reason=args.reason,
        group=args.group,
        region=args.region,
    )
    print(json.dumps(payload, indent=2, ensure_ascii=False))
    return 0 if payload.get("ok") else 2


if __name__ == "__main__":
    raise SystemExit(main())
