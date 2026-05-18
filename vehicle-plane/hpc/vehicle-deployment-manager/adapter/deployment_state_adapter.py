#!/usr/bin/env python3
"""Read-only Primary/Candidate/Shadow view over legacy deploy/state files.

This Phase 3-B prototype intentionally does not modify any existing deployment
state, C++ code, dashboard code, status-server code, scripts, or JSON schemas.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


JsonObject = Dict[str, Any]
DEFAULT_FEATURES = ["ADAS", "Navigation", "Energy"]


def _read_json_file(path: Path) -> Tuple[Optional[Any], Optional[str]]:
    try:
        if not path.exists():
            return None, f"missing file: {path.as_posix()}"
        with path.open("r", encoding="utf-8") as f:
            return json.load(f), None
    except json.JSONDecodeError as exc:
        return None, f"invalid JSON in {path.as_posix()}: {exc}"
    except OSError as exc:
        return None, f"failed to read {path.as_posix()}: {exc}"


def _normalize_feature(value: Any) -> str:
    raw = str(value or "").strip()
    compact = raw.lower().replace("_", "").replace("-", "").replace(" ", "")
    if compact in {"adas", "adasfeature"}:
        return "ADAS"
    if compact in {"navigation", "nav", "navigationfeature"}:
        return "Navigation"
    if compact in {"energy", "evenergy", "ev", "evenergyfeature"}:
        return "Energy"
    return raw


def _bool_value(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in {"true", "1", "yes", "on", "enabled"}
    return bool(value)


def _clean_string(value: Any) -> Optional[str]:
    if value is None:
        return None
    text = str(value).strip()
    return text or None


def _increment(counter: JsonObject, key: Optional[str]) -> None:
    if not key:
        key = "unknown"
    counter[key] = int(counter.get(key, 0)) + 1


class DeploymentStateAdapter:
    """Read-only adapter from legacy vehicle state JSON to Primary/Candidate views."""

    def __init__(self, root: str | Path = ".") -> None:
        self.root = Path(root).resolve()
        self.deploy_root = self.root / "sdv-ota-manager" / "platform" / "deploy"
        self.state_root = self.deploy_root / "state"
        self.rollback_root = self.deploy_root / "rollback"
        self.history_path = self.deploy_root / "history" / "fleet_history.jsonl"
        self.warnings: List[str] = []

    def load_vehicle_state(self, vehicle_id: str) -> JsonObject:
        vehicle = str(vehicle_id or "").strip()
        if not vehicle:
            self._add_warning("vehicle_id is empty")
            return {}
        path = self.state_root / f"{vehicle}.json"
        data, warning = _read_json_file(path)
        if warning:
            self._add_warning(warning)
            return {}
        if not isinstance(data, dict):
            self._add_warning(f"vehicle state is not an object: {path.as_posix()}")
            return {}
        return data

    def load_all_vehicle_states(self) -> Dict[str, JsonObject]:
        states: Dict[str, JsonObject] = {}
        for vehicle_id in self.list_vehicle_ids():
            state = self.load_vehicle_state(vehicle_id)
            if state:
                states[vehicle_id] = state
        return states

    def list_vehicle_ids(self) -> List[str]:
        if not self.state_root.exists():
            self._add_warning(f"missing state directory: {self.state_root.as_posix()}")
            return []
        ids = []
        for path in self.state_root.glob("car_*.json"):
            ids.append(path.stem)
        return sorted(ids, key=self._vehicle_sort_key)

    def get_vehicle_primary(self, vehicle_id: str, feature: str) -> Optional[str]:
        raw = self.load_vehicle_state(vehicle_id)
        block = self._feature_block(raw, feature)
        if block:
            return (
                _clean_string(block.get("primary_version"))
                or _clean_string(block.get("active_version"))
            )
        suffix = self._legacy_suffix(feature)
        return (
            _clean_string(raw.get(f"primary_{suffix}"))
            or _clean_string(raw.get(f"active_{suffix}"))
        )

    def get_vehicle_candidate(self, vehicle_id: str, feature: str) -> Optional[str]:
        raw = self.load_vehicle_state(vehicle_id)
        block = self._feature_block(raw, feature)
        if block:
            shadow = self.get_shadow_status(vehicle_id, feature)
            if shadow["enabled"] and shadow["version"]:
                return shadow["version"]
            return (
                _clean_string(block.get("pending_active_version"))
                or _clean_string(block.get("shadow_version"))
            )
        suffix = self._legacy_suffix(feature)
        return (
            _clean_string(raw.get(f"pending_active_{suffix}"))
            or _clean_string(raw.get("shadow_version"))
        )

    def get_shadow_status(self, vehicle_id: str, feature: str) -> JsonObject:
        raw = self.load_vehicle_state(vehicle_id)
        block = self._feature_block(raw, feature)
        source = block if block else raw
        enabled = _bool_value(source.get("shadow_enabled", False))
        return {
            "enabled": enabled,
            "version": _clean_string(source.get("shadow_version")),
            "status": _clean_string(source.get("shadow_status")),
            "validation_result": _clean_string(source.get("shadow_validation_result")),
            "failure_reason": _clean_string(source.get("shadow_failure_reason")),
        }

    def get_kill_switch_status(self, vehicle_id: str, feature: str) -> JsonObject:
        raw = self.load_vehicle_state(vehicle_id)
        block = self._feature_block(raw, feature)
        source = block if block else raw
        return {
            "enabled": _bool_value(source.get("kill_switch", False)),
            "reason": _clean_string(source.get("kill_reason")),
        }

    def get_rollback_info(self, vehicle_id: str, feature: str) -> JsonObject:
        raw = self.load_vehicle_state(vehicle_id)
        block = self._feature_block(raw, feature)
        stack = []
        if block and isinstance(block.get("rollback_stack"), list):
            stack = [x for x in block.get("rollback_stack", []) if isinstance(x, dict)]

        rollback_file = self.rollback_root / str(vehicle_id) / f"{_normalize_feature(feature)}.json"
        rollback_file_data: Optional[JsonObject] = None
        if rollback_file.exists():
            data, warning = _read_json_file(rollback_file)
            if warning:
                self._add_warning(warning)
            elif isinstance(data, dict):
                rollback_file_data = data

        previous_primary = None
        if stack:
            previous_primary = _clean_string(stack[0].get("version"))
        if not previous_primary and rollback_file_data:
            previous_primary = (
                _clean_string(rollback_file_data.get("previous_version"))
                or _clean_string(rollback_file_data.get("restored_version"))
            )

        return {
            "available": bool(stack or rollback_file_data),
            "previous_primary": previous_primary,
            "rollback_stack": stack,
            "rollback_file": rollback_file.as_posix() if rollback_file.exists() else None,
        }

    def to_primary_candidate_view(self, vehicle_id: str, raw_state: Optional[JsonObject] = None) -> JsonObject:
        raw = raw_state if isinstance(raw_state, dict) else self.load_vehicle_state(vehicle_id)
        vehicle = _clean_string(raw.get("car_id")) or str(vehicle_id)
        features: JsonObject = {}
        for feature in self._features_in_state(raw):
            block = self._feature_block(raw, feature)
            shadow = self._shadow_status_from_raw(raw, feature)
            kill = self._kill_switch_from_raw(raw, feature)
            rollback = self._rollback_info_from_raw(vehicle, feature, raw)

            primary = self._primary_from_raw(raw, feature, block)
            candidate = self._candidate_from_raw(raw, feature, block, shadow)
            candidate_state = self._candidate_state(candidate, shadow, block)

            features[feature] = {
                "primary_version": primary,
                "candidate_version": candidate,
                "candidate_state": candidate_state,
                "shadow_mode": shadow,
                "kill_switch": kill,
                "rollback": {
                    "available": rollback["available"],
                    "previous_primary": rollback["previous_primary"],
                    "rollback_file": rollback["rollback_file"],
                    "rollback_stack": rollback["rollback_stack"],
                },
            }

        return {
            "vehicle_id": vehicle,
            "model": _clean_string(raw.get("model")),
            "region": _clean_string(raw.get("region")),
            "group": _clean_string(raw.get("group")),
            "status": _clean_string(raw.get("status")),
            "features": features,
            "ota": raw.get("ota") if isinstance(raw.get("ota"), dict) else {},
            "warnings": self.warnings,
        }

    def summarize_fleet_state(self) -> JsonObject:
        states = self.load_all_vehicle_states()
        feature_summary: JsonObject = {}

        for vehicle_id, raw in states.items():
            view = self.to_primary_candidate_view(vehicle_id, raw)
            for feature, feature_view in view.get("features", {}).items():
                item = feature_summary.setdefault(feature, {
                    "primary_versions": {},
                    "candidate_versions": {},
                    "shadow_running": 0,
                    "kill_switch_active": 0,
                    "rollback_available": 0,
                })
                _increment(item["primary_versions"], feature_view.get("primary_version"))
                _increment(item["candidate_versions"], feature_view.get("candidate_version"))
                shadow = feature_view.get("shadow_mode") or {}
                if shadow.get("enabled") and str(shadow.get("status") or "").upper() == "RUNNING":
                    item["shadow_running"] += 1
                kill = feature_view.get("kill_switch") or {}
                if kill.get("enabled"):
                    item["kill_switch_active"] += 1
                rollback = feature_view.get("rollback") or {}
                if rollback.get("available"):
                    item["rollback_available"] += 1

        return {
            "ok": not self.warnings,
            "root": self.root.as_posix(),
            "state_root": self.state_root.as_posix(),
            "vehicles": len(states),
            "features": feature_summary,
            "history_file": self.history_path.as_posix(),
            "warnings": self.warnings,
        }

    def _add_warning(self, message: str) -> None:
        if message not in self.warnings:
            self.warnings.append(message)

    @staticmethod
    def _vehicle_sort_key(vehicle_id: str) -> Tuple[str, int]:
        prefix, _, suffix = vehicle_id.partition("_")
        try:
            return (prefix, int(suffix))
        except ValueError:
            return (vehicle_id, 0)

    def _features_in_state(self, raw: JsonObject) -> List[str]:
        found = set(DEFAULT_FEATURES)
        features = raw.get("features")
        if isinstance(features, dict):
            for key in features.keys():
                normalized = _normalize_feature(key)
                if normalized:
                    found.add(normalized)

        for key in raw.keys():
            for prefix in ("active_", "primary_", "pending_active_"):
                if key.startswith(prefix):
                    found.add(_normalize_feature(key[len(prefix):]))

        return sorted(found)

    def _feature_block(self, raw: JsonObject, feature: str) -> JsonObject:
        features = raw.get("features")
        target = _normalize_feature(feature)
        if isinstance(features, dict):
            for key, value in features.items():
                if _normalize_feature(key) == target and isinstance(value, dict):
                    return value
        direct = raw.get(target)
        return direct if isinstance(direct, dict) else {}

    def _legacy_suffix(self, feature: str) -> str:
        return _normalize_feature(feature)

    def _primary_from_raw(self, raw: JsonObject, feature: str, block: JsonObject) -> Optional[str]:
        suffix = self._legacy_suffix(feature)
        return (
            _clean_string(block.get("primary_version"))
            or _clean_string(block.get("active_version"))
            or _clean_string(raw.get(f"primary_{suffix}"))
            or _clean_string(raw.get(f"active_{suffix}"))
        )

    def _candidate_from_raw(
        self,
        raw: JsonObject,
        feature: str,
        block: JsonObject,
        shadow: JsonObject,
    ) -> Optional[str]:
        suffix = self._legacy_suffix(feature)
        if shadow.get("enabled") and shadow.get("version"):
            return _clean_string(shadow.get("version"))
        return (
            _clean_string(block.get("pending_active_version"))
            or _clean_string(block.get("shadow_version"))
            or _clean_string(raw.get(f"pending_active_{suffix}"))
            or _clean_string(raw.get("shadow_version"))
        )

    def _shadow_status_from_raw(self, raw: JsonObject, feature: str) -> JsonObject:
        block = self._feature_block(raw, feature)
        source = block if block else raw
        return {
            "enabled": _bool_value(source.get("shadow_enabled", False)),
            "version": _clean_string(source.get("shadow_version")),
            "status": _clean_string(source.get("shadow_status")),
            "validation_result": _clean_string(source.get("shadow_validation_result")),
            "failure_reason": _clean_string(source.get("shadow_failure_reason")),
        }

    def _kill_switch_from_raw(self, raw: JsonObject, feature: str) -> JsonObject:
        block = self._feature_block(raw, feature)
        source = block if block else raw
        return {
            "enabled": _bool_value(source.get("kill_switch", False)),
            "reason": _clean_string(source.get("kill_reason")),
        }

    def _rollback_info_from_raw(self, vehicle_id: str, feature: str, raw: JsonObject) -> JsonObject:
        block = self._feature_block(raw, feature)
        stack = []
        if isinstance(block.get("rollback_stack"), list):
            stack = [x for x in block.get("rollback_stack", []) if isinstance(x, dict)]

        rollback_file = self.rollback_root / vehicle_id / f"{_normalize_feature(feature)}.json"
        rollback_file_data: Optional[JsonObject] = None
        if rollback_file.exists():
            data, warning = _read_json_file(rollback_file)
            if warning:
                self._add_warning(warning)
            elif isinstance(data, dict):
                rollback_file_data = data

        previous_primary = None
        if stack:
            previous_primary = _clean_string(stack[0].get("version"))
        if not previous_primary and rollback_file_data:
            previous_primary = (
                _clean_string(rollback_file_data.get("previous_version"))
                or _clean_string(rollback_file_data.get("restored_version"))
            )

        return {
            "available": bool(stack or rollback_file_data),
            "previous_primary": previous_primary,
            "rollback_stack": stack,
            "rollback_file": rollback_file.as_posix() if rollback_file.exists() else None,
        }

    def _candidate_state(self, candidate: Optional[str], shadow: JsonObject, block: JsonObject) -> str:
        if shadow.get("enabled"):
            status = str(shadow.get("status") or "").strip().upper()
            if status:
                return f"SHADOW_{status}"
            return "SHADOW_ENABLED"
        if candidate:
            return "PENDING"
        if block.get("pending_active_version"):
            return "PENDING"
        return "NONE"


def main() -> int:
    parser = argparse.ArgumentParser(description="Read-only Primary/Candidate view over legacy vehicle state files.")
    parser.add_argument("--root", default=".", help="Repository root path (default: current directory)")
    parser.add_argument("--summary", action="store_true", help="Print fleet summary JSON")
    parser.add_argument("--vehicle", default="", help="Vehicle id, e.g. car_1")
    parser.add_argument("--feature", default="", help="Optional feature filter, e.g. ADAS")
    args = parser.parse_args()

    adapter = DeploymentStateAdapter(args.root)

    if args.summary:
        print(json.dumps(adapter.summarize_fleet_state(), indent=2, ensure_ascii=False))
        return 0

    if args.vehicle:
        raw = adapter.load_vehicle_state(args.vehicle)
        view = adapter.to_primary_candidate_view(args.vehicle, raw)
        if args.feature:
            feature = _normalize_feature(args.feature)
            view["features"] = {
                feature: view.get("features", {}).get(feature, {
                    "primary_version": None,
                    "candidate_version": None,
                    "candidate_state": "NONE",
                    "shadow_mode": {"enabled": False, "version": None, "status": None},
                    "kill_switch": {"enabled": False, "reason": None},
                    "rollback": {"available": False, "previous_primary": None, "rollback_stack": []},
                })
            }
        print(json.dumps(view, indent=2, ensure_ascii=False))
        return 0

    print(json.dumps(adapter.summarize_fleet_state(), indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
