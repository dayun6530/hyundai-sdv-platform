#!/usr/bin/env python3
"""Verify CARLA Python API responds (port open != simulator ready)."""
from __future__ import annotations

import argparse
import glob
import os
import sys
import time


def _bootstrap_egg(examples_dir: str) -> None:
    egg_glob = os.path.join(examples_dir, "..", "carla", "dist", "carla-*%d.%d-%s.egg") % (
        sys.version_info.major,
        sys.version_info.minor,
        "win-amd64" if os.name == "nt" else "linux-x86_64",
    )
    matches = glob.glob(egg_glob)
    if not matches:
        raise SystemExit(f"CARLA Python egg not found: {egg_glob}")
    sys.path.insert(0, matches[0])


def main() -> int:
    parser = argparse.ArgumentParser(description="Wait for CARLA Python API (get_world)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("-p", "--port", type=int, default=2000)
    parser.add_argument("--timeout", type=int, default=90, help="Total retry window (seconds)")
    parser.add_argument("--per-call", type=float, default=25.0, help="carla.Client timeout per attempt")
    parser.add_argument("--carla-root", default="", help="WindowsNoEditor path (for egg bootstrap)")
    args = parser.parse_args()

    if args.carla_root:
        examples = os.path.join(args.carla_root, "PythonAPI", "examples")
        if os.path.isdir(examples):
            os.chdir(examples)
            _bootstrap_egg(examples)
    else:
        # Assume cwd is PythonAPI/examples (start_carla.ps1 Push-Location).
        _bootstrap_egg(os.getcwd())

    import carla  # noqa: WPS433

    deadline = time.time() + args.timeout
    attempt = 0
    last_err = ""
    while time.time() < deadline:
        attempt += 1
        try:
            client = carla.Client(args.host, args.port)
            client.set_timeout(args.per_call)
            world = client.get_world()
            map_name = world.get_map().name
            print("OK", client.get_server_version(), map_name)
            return 0
        except RuntimeError as exc:
            last_err = str(exc)
            print(f"[verify] attempt {attempt}: {exc}", file=sys.stderr)
            time.sleep(3.0)

    print(
        f"TIMEOUT: CARLA API not ready on {args.host}:{args.port} after {args.timeout}s\n"
        f"Last error: {last_err}\n"
        "Fix:\n"
        "  1) .\\stop_carla.ps1\n"
        "  2) .\\start_carla.ps1\n"
        "  (Port :2000 can open before the map finishes loading.)",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
