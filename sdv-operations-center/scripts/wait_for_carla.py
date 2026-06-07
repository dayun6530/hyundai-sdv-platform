#!/usr/bin/env python3
"""Wait until CARLA RPC port accepts connections (default 127.0.0.1:2000)."""
from __future__ import annotations

import argparse
import socket
import sys
import time


def port_open(host: str, port: int, timeout_sec: float = 1.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout_sec):
            return True
    except OSError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Wait for CARLA simulator port")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("-p", "--port", type=int, default=2000)
    parser.add_argument("--timeout", type=int, default=120, help="Max wait seconds")
    args = parser.parse_args()

    deadline = time.time() + args.timeout
    print(f"Waiting for CARLA at {args.host}:{args.port} (max {args.timeout}s)...")
    while time.time() < deadline:
        if port_open(args.host, args.port):
            print(f"CARLA ready on {args.host}:{args.port}")
            return 0
        time.sleep(1.0)

    print(
        f"TIMEOUT: no simulator on {args.host}:{args.port}\n"
        "Start CARLA first, e.g.:\n"
        "  cd CARLA_0.9.15\\WindowsNoEditor\n"
        "  .\\CarlaUE4.exe -dx11 -nosound -windowed -ResX=800 -ResY=600 -quality-level=Low\n"
        "Wait until the map is fully loaded (15-60s), then retry.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
