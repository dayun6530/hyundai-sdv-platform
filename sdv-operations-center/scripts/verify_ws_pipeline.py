#!/usr/bin/env python3
"""Verify mock/bridge WebSocket telemetry changes over time (Phase 1→2 wire format)."""

from __future__ import annotations

import asyncio
import json
import sys

URL = "ws://127.0.0.1:9090"
DURATION_SEC = 8.0


async def main() -> int:
    try:
        import websockets
    except ImportError:
        print("ERROR: pip install websockets")
        return 2

    samples: list[tuple[str, float, float, float]] = []

    async with websockets.connect(URL) as ws:
        deadline = asyncio.get_event_loop().time() + DURATION_SEC
        while asyncio.get_event_loop().time() < deadline:
            raw = await asyncio.wait_for(ws.recv(), timeout=3.0)
            msg = json.loads(raw)
            etype = msg.get("type", "")
            data = msg.get("data") or {}

            if etype == "ego_pose":
                pos = data.get("position") or {}
                speed = float(data.get("speed_mps", 0))
                samples.append(("ego_pose", float(pos.get("x", 0)), float(pos.get("y", 0)), speed))
            elif etype == "vehicle_state":
                speed = float(data.get("speed_mps", data.get("longitudinal_velocity_mps", 0)))
                samples.append(("vehicle_state", 0.0, 0.0, speed))

    if len(samples) < 5:
        print(f"FAIL: only {len(samples)} telemetry samples in {DURATION_SEC}s")
        return 1

    xs = [s[1] for s in samples if s[0] == "ego_pose"]
    speeds = [s[3] for s in samples if s[0] in ("ego_pose", "vehicle_state")]

    x_delta = max(xs) - min(xs) if xs else 0.0
    speed_delta = max(speeds) - min(speeds) if speeds else 0.0

    print(f"Samples: {len(samples)} | ego x range: {x_delta:.2f} m | speed range: {speed_delta:.2f} m/s")
    print(f"Last ego: x={samples[-1][1]:.1f} y={samples[-1][2]:.1f} speed={samples[-1][3]*3.6:.0f} km/h")

    if x_delta < 1.0 and speed_delta < 0.5:
        print("FAIL: values did not change enough — live stream not working")
        return 1

    print("PASS: WebSocket telemetry is updating in real time")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
