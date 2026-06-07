#!/usr/bin/env python3
"""Verify CARLA Python API and agents (BasicAgent, BehaviorAgent, GlobalRoutePlanner).

Exit code 0 = all available, 1 = partial, 2 = carla missing.

Usage:
  python verify_carla_agents.py
  set CARLA_ROOT=C:\\path\\to\\CARLA_0.9.15
"""

from __future__ import annotations

import os
import sys


def main() -> int:
    results: dict[str, str] = {}

    try:
        import carla  # noqa: F401

        results["carla"] = "PASS"
    except ImportError as exc:
        results["carla"] = f"FAIL ({exc})"
        _print_report(results)
        return 2

    carla_root = os.environ.get("CARLA_ROOT", "").strip()
    if carla_root:
        agents_dir = os.path.join(carla_root, "PythonAPI", "carla", "agents")
        if os.path.isdir(agents_dir) and agents_dir not in sys.path:
            sys.path.insert(0, agents_dir)
        extra = os.path.join(carla_root, "PythonAPI", "carla")
        if os.path.isdir(extra) and extra not in sys.path:
            sys.path.insert(0, extra)

    for name, mod, cls in (
        ("GlobalRoutePlanner", "agents.navigation.global_route_planner", "GlobalRoutePlanner"),
        ("BasicAgent", "agents.navigation.basic_agent", "BasicAgent"),
        ("BehaviorAgent", "agents.navigation.behavior_agent", "BehaviorAgent"),
    ):
        try:
            module = __import__(mod, fromlist=[cls])
            getattr(module, cls)
            results[name] = "PASS"
        except Exception as exc:
            results[name] = f"FAIL ({type(exc).__name__}: {exc})"

    _print_report(results)
    fails = sum(1 for v in results.values() if v.startswith("FAIL"))
    if fails == 0:
        return 0
    if results.get("carla") == "PASS":
        return 1
    return 2


def _print_report(results: dict[str, str]) -> None:
    print("CARLA agents verification")
    print("-" * 48)
    for key in ("carla", "GlobalRoutePlanner", "BasicAgent", "BehaviorAgent"):
        print(f"  {key:22} {results.get(key, 'SKIP')}")
    if os.environ.get("CARLA_ROOT"):
        print(f"  CARLA_ROOT            {os.environ['CARLA_ROOT']}")
    else:
        print("  CARLA_ROOT            (not set - add PythonAPI/carla/agents to PYTHONPATH)")


if __name__ == "__main__":
    sys.exit(main())
