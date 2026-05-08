#!/usr/bin/env python3
"""
Sync deploy/vehicle_runtime/fleet_feature_versions.json from deploy/vehicle_runtime/car_*.json.

- Source of truth: car runtime state JSON (per car, per domain primary_version).
- Target: fleet_feature_versions.json (per car, per feature key).

Feature key normalization:
  - ADAS      <- domains.adas.primary_version
  - Navigation<- domains.navigation.primary_version
  - EvEnergy  <- domains.evenergy.primary_version

Notes:
  - Versions are stored as tag suffixes without leading 'v' (e.g. "2.0").
  - If a car/domain is missing or version is empty, this script leaves fleet value unchanged by default.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any, Dict, Iterable, Tuple


OTA_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_FLEET = os.path.join(OTA_ROOT, "deploy", "vehicle_runtime", "fleet_feature_versions.json")
DEFAULT_RUNTIME_DIR = os.path.join(OTA_ROOT, "deploy", "vehicle_runtime")

ALL_CARS = [f"car_{i}" for i in range(1, 11)]

DOMAIN_TO_FEATURE: Tuple[Tuple[str, str], ...] = (
    ("adas", "ADAS"),
    ("navigation", "Navigation"),
    ("evenergy", "EvEnergy"),
)


def normalize_version(v: Any) -> str:
    s = str(v or "").strip()
    if not s:
        return ""
    if len(s) >= 2 and s[0] in "vV" and s[1].isdigit():
        return s[1:]
    return s


def load_json(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        d = json.load(f)
    if not isinstance(d, dict):
        raise ValueError(f"Expected object JSON: {path}")
    return d


def save_json(path: str, data: Dict[str, Any]) -> None:
    """
    Write JSON in a human-scannable format.

    For fleet_feature_versions.json we keep each car entry on a single line
    so it stays readable at a glance in the editor.
    """
    if os.path.basename(path) == "fleet_feature_versions.json":
        save_fleet_feature_versions_json(path, data)
        return
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


def _q(s: Any) -> str:
    return json.dumps(str(s), ensure_ascii=False)


def save_fleet_feature_versions_json(path: str, data: Dict[str, Any]) -> None:
    schema_version = data.get("schema_version", 1)
    comment = data.get("comment", "")
    defaults = data.get("defaults") if isinstance(data.get("defaults"), dict) else {}
    cars = data.get("cars") if isinstance(data.get("cars"), dict) else {}

    # Ensure key normalization even if caller didn't.
    if "Energy" in defaults and "EvEnergy" not in defaults:
        defaults["EvEnergy"] = defaults.get("Energy")

    lines: list[str] = []
    lines.append("{")
    lines.append(f'  "schema_version": {int(schema_version)},')
    lines.append(f'  "comment": {_q(comment)},')
    lines.append('  "defaults": {')
    lines.append(f'    "ADAS": {_q(defaults.get("ADAS", "1.0"))},')
    lines.append(f'    "Navigation": {_q(defaults.get("Navigation", "1.0"))},')
    lines.append(f'    "EvEnergy": {_q(defaults.get("EvEnergy", "1.0"))}')
    lines.append("  },")
    lines.append('  "cars": {')

    car_ids = sorted([k for k in cars.keys() if isinstance(k, str)], key=lambda x: (x != "car_1", x))
    for i, car_id in enumerate(car_ids):
        feats = cars.get(car_id) if isinstance(cars.get(car_id), dict) else {}
        if "Energy" in feats and "EvEnergy" not in feats:
            feats["EvEnergy"] = feats.get("Energy")
        adas = feats.get("ADAS", defaults.get("ADAS", "1.0"))
        nav = feats.get("Navigation", defaults.get("Navigation", "1.0"))
        eve = feats.get("EvEnergy", defaults.get("EvEnergy", "1.0"))
        comma = "," if i + 1 < len(car_ids) else ""
        lines.append(
            f'    "{car_id}": {{ "ADAS": {_q(adas)}, "Navigation": {_q(nav)}, "EvEnergy": {_q(eve)} }}{comma}'
        )

    lines.append("  }")
    lines.append("}")
    lines.append("")

    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def iter_runtime_states(runtime_dir: str, cars: Iterable[str]) -> Iterable[Tuple[str, Dict[str, Any]]]:
    for car in cars:
        p = os.path.join(runtime_dir, f"{car}.json")
        if not os.path.isfile(p):
            continue
        try:
            yield car, load_json(p)
        except Exception as e:
            raise RuntimeError(f"Failed to read {p}: {e}") from e


def ensure_fleet_shape(fleet: Dict[str, Any]) -> None:
    if not isinstance(fleet.get("defaults"), dict):
        fleet["defaults"] = {}
    if not isinstance(fleet.get("cars"), dict):
        fleet["cars"] = {}
    # Backward compat: Energy -> EvEnergy (prefer EvEnergy)
    if "Energy" in fleet["defaults"] and "EvEnergy" not in fleet["defaults"]:
        fleet["defaults"]["EvEnergy"] = fleet["defaults"].get("Energy")
    for car_id, feats in list(fleet["cars"].items()):
        if not isinstance(feats, dict):
            continue
        if "Energy" in feats and "EvEnergy" not in feats:
            feats["EvEnergy"] = feats.get("Energy")


def upsert_car_feature(fleet: Dict[str, Any], car: str, feature: str, version_suffix: str) -> None:
    cars = fleet.setdefault("cars", {})
    blk = cars.get(car)
    if not isinstance(blk, dict):
        blk = {}
        cars[car] = blk
    blk[feature] = version_suffix


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fleet", default=DEFAULT_FLEET, help="Path to fleet_feature_versions.json")
    ap.add_argument(
        "--runtime-dir",
        default=DEFAULT_RUNTIME_DIR,
        help="Directory containing deploy/vehicle_runtime/car_*.json",
    )
    ap.add_argument(
        "--cars",
        default=",".join(ALL_CARS),
        help="Comma-separated car ids (default: car_1..car_10)",
    )
    ap.add_argument(
        "--update-missing",
        action="store_true",
        help="If a car doesn't exist in fleet.cars, create it before setting values",
    )
    ap.add_argument(
        "--write-defaults-if-unset",
        action="store_true",
        help="If fleet.defaults.<feature> is missing, set it from car_1 runtime values (best-effort)",
    )
    args = ap.parse_args()

    fleet = load_json(args.fleet)
    ensure_fleet_shape(fleet)

    cars = [c.strip() for c in str(args.cars).split(",") if c.strip()]
    for car, state in iter_runtime_states(args.runtime_dir, cars):
        domains = state.get("domains") or {}
        if not isinstance(domains, dict):
            continue
        for dom, feature in DOMAIN_TO_FEATURE:
            cur = domains.get(dom) or {}
            if not isinstance(cur, dict):
                continue
            pv = normalize_version(cur.get("primary_version"))
            if not pv:
                continue
            if not args.update_missing and car not in (fleet.get("cars") or {}):
                continue
            upsert_car_feature(fleet, car, feature, pv)

    if args.write_defaults_if_unset:
        defaults = fleet.setdefault("defaults", {})
        if isinstance(defaults, dict):
            car1_path = os.path.join(args.runtime_dir, "car_1.json")
            if os.path.isfile(car1_path):
                s1 = load_json(car1_path)
                d1 = (s1.get("domains") or {}) if isinstance(s1.get("domains"), dict) else {}
                for dom, feature in DOMAIN_TO_FEATURE:
                    if feature in defaults and str(defaults.get(feature, "")).strip():
                        continue
                    cur = d1.get(dom) or {}
                    pv = normalize_version((cur or {}).get("primary_version"))
                    if pv:
                        defaults[feature] = pv

    save_json(args.fleet, fleet)
    return 0


if __name__ == "__main__":
    sys.exit(main())

