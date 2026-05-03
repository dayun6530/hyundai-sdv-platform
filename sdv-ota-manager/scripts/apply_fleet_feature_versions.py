#!/usr/bin/env python3
"""
Apply per-car, per-feature-line Docker versions using ota_vehicle.sh bootstrap.

Config: deploy/vehicle_runtime/fleet_feature_versions.json
  - defaults: ADAS, Navigation, Energy (tag suffix, e.g. 1.0 or v1.0)
  - cars: optional overrides per car_id; missing keys fall back to defaults.

Energy maps to ota_vehicle --domain EvEnergy (image sdv/evenergy-feature:v*).
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from typing import Any, Dict

OTA_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OTA_VEHICLE = os.path.join(OTA_ROOT, "scripts", "ota_vehicle.sh")
DEFAULT_CONFIG = os.path.join(
    OTA_ROOT, "deploy", "vehicle_runtime", "fleet_feature_versions.json"
)

ALL_CARS = [f"car_{i}" for i in range(1, 11)]
FEATURE_KEYS = ("ADAS", "Navigation", "Energy")
# ota_vehicle.sh --domain values
OTA_DOMAIN = {"ADAS": "ADAS", "Navigation": "Navigation", "Energy": "EvEnergy"}


def normalize_version(v: Any) -> str:
    s = str(v or "").strip()
    if not s:
        return s
    if s[0] in "vV" and len(s) > 1 and s[1].isdigit():
        return s[1:]
    return s


def load_config(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data.get("defaults"), dict):
        data["defaults"] = {}
    if not isinstance(data.get("cars"), dict):
        data["cars"] = {}
    return data


def merged_per_car(data: Dict[str, Any]) -> Dict[str, Dict[str, str]]:
    defaults = data.get("defaults") or {}
    cars = data.get("cars") or {}
    out: Dict[str, Dict[str, str]] = {}
    for car in ALL_CARS:
        base = {k: str(defaults.get(k, "1.0")).strip() for k in FEATURE_KEYS}
        ov = cars.get(car) or {}
        if isinstance(ov, dict):
            for k in FEATURE_KEYS:
                if k in ov and str(ov[k]).strip():
                    base[k] = str(ov[k]).strip()
        out[car] = base
    return out


def validate_config(data: Dict[str, Any]) -> str:
    if not isinstance(data.get("defaults"), dict):
        return "defaults must be an object"
    cars = data.get("cars")
    if not isinstance(cars, dict):
        return "cars must be an object"
    for k in FEATURE_KEYS:
        if k not in data["defaults"]:
            return f"defaults missing key: {k}"
    for car, feats in cars.items():
        if car not in ALL_CARS:
            return f"unknown car id: {car}"
        if not isinstance(feats, dict):
            return f"cars.{car} must be an object"
        for fk in feats:
            if fk not in FEATURE_KEYS:
                return f"cars.{car} unknown feature key: {fk}"
    return ""


def run_bootstrap(car: str, feature_key: str, version: str, dry_run: bool) -> list[str]:
    dom = OTA_DOMAIN[feature_key]
    ver = normalize_version(version)
    if not ver:
        raise ValueError(f"empty version for {car} {feature_key}")
    cmd = [
        "bash",
        OTA_VEHICLE,
        "bootstrap",
        "--car",
        car,
        "--domain",
        dom,
        "--version",
        ver,
    ]
    line = " ".join(cmd)
    if dry_run:
        return [f"[dry-run] {line}"]
    p = subprocess.run(
        cmd,
        cwd=OTA_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    out = [line]
    if p.stdout:
        out.append(p.stdout.rstrip())
    if p.stderr:
        out.append(p.stderr.rstrip())
    if p.returncode != 0:
        raise RuntimeError(
            f"bootstrap failed exit={p.returncode} {car} {dom} v{ver}\n"
            + (p.stderr or p.stdout or "")
        )
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--config",
        default=DEFAULT_CONFIG,
        help="Path to fleet_feature_versions.json",
    )
    ap.add_argument(
        "--dry-run",
        action="store_true",
        help="Print bootstrap commands without running docker",
    )
    args = ap.parse_args()

    if not os.path.isfile(OTA_VEHICLE):
        print(f"Missing ota_vehicle.sh: {OTA_VEHICLE}", file=sys.stderr)
        return 2

    data = load_config(args.config)
    err = validate_config(data)
    if err:
        print(f"Invalid config: {err}", file=sys.stderr)
        return 2

    merged = merged_per_car(data)
    lines: list[str] = []
    try:
        for car in ALL_CARS:
            feats = merged[car]
            for fk in FEATURE_KEYS:
                chunk = run_bootstrap(car, fk, feats[fk], args.dry_run)
                lines.extend(chunk)
                lines.append(f"[ok] {car} {fk} -> {OTA_DOMAIN[fk]} v{normalize_version(feats[fk])}")
    except Exception as e:
        print("\n".join(lines), file=sys.stderr)
        print(str(e), file=sys.stderr)
        return 1

    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
