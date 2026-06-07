#!/usr/bin/env python3
"""Mock sdv_ros_bridge WebSocket for pipeline smoke tests (no ROS required)."""

from __future__ import annotations

import asyncio
import base64
import io
import json
import math
import time

HOST = "127.0.0.1"
PORT = 9090

# Local map frame: drive along +X in meters (projects ~east on Mapbox at default Seoul origin).
DEMO_CENTER_X = 100.0
DEMO_CENTER_Y = 200.0
DEMO_SEG_LEN_M = 5000.0
DEMO_SPEED_MPS = 14.0


def envelope(etype: str, stamp_ns: int, frame_id: str, data: dict) -> str:
    return json.dumps(
        {
            "schema_version": 1,
            "type": etype,
            "stamp_ns": stamp_ns,
            "stamp_iso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "frame_id": frame_id,
            "data": data,
        }
    )


def make_front_camera_b64(t: float) -> str:
    """Synthetic driver-view JPEG for CarlaCameraReceiver (CARLA-like road preview)."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return ""

    img = Image.new("RGB", (1280, 720), (24, 32, 48))
    draw = ImageDraw.Draw(img)
    for y in range(300):
        shade = int(18 + y * 0.22)
        draw.line([(0, y), (1279, y)], fill=(shade, shade + 12, shade + 28))
    draw.rectangle([0, 300, 1279, 719], fill=(62, 70, 84))
    for lane_x in (380, 520, 640, 760, 900):
        for y in range(310, 700, 34):
            draw.rectangle([lane_x, y, lane_x + 10, y + 16], fill=(245, 245, 245))
    draw.rectangle([560, 520, 720, 700], fill=(34, 197, 94, 64))
    draw.text((24, 18), "CARLA 0.9.15 · mock front_camera", fill=(186, 230, 253))
    draw.text((24, 680), f"t={t:.1f}s · 3-lane urban", fill=(203, 213, 225))
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=86)
    return base64.b64encode(buf.getvalue()).decode("ascii")


def demo_pose(traveled_m: float) -> tuple[float, float, float]:
    """Forward-only along +X; yaw matches travel direction (no reverse leg)."""
    u = traveled_m % DEMO_SEG_LEN_M
    x = DEMO_CENTER_X + u
    y = DEMO_CENTER_Y
    return x, y, 0.0


async def client_handler(websocket) -> None:
    await websocket.send(
        envelope("bridge_hello", time.time_ns(), "", {"service": "mock-sdv-ros-bridge"})
    )
    t0 = time.monotonic()
    while True:
        t = time.monotonic() - t0
        stamp = time.time_ns()
        traveled = t * DEMO_SPEED_MPS
        x, y, yaw = demo_pose(traveled)
        speed = DEMO_SPEED_MPS

        await websocket.send(
            envelope(
                "vehicle_state",
                stamp,
                "base_link",
                {
                    "stamp_ns": stamp,
                    "frame_id": "base_link",
                    "longitudinal_velocity_mps": speed if yaw == 0.0 else -speed,
                    "lateral_velocity_mps": 0.0,
                    "heading_rate_rps": 0.0,
                    "speed_mps": speed,
                },
            )
        )
        await websocket.send(
            envelope(
                "ego_pose",
                stamp,
                "map",
                {
                    "stamp_ns": stamp,
                    "frame_id": "map",
                    "position": {"x": x, "y": y, "z": 0.5},
                    "orientation": {"w": 1.0, "x": 0.0, "y": 0.0, "z": 0.0},
                    "yaw_rad": yaw,
                    "speed_mps": speed,
                },
            )
        )

        traj_pts = []
        ahead_len = 90.0
        n_pts = 48
        for i in range(n_pts + 1):
            d = (ahead_len * i) / n_pts
            traj_pts.append(
                {
                    "x": x + d * math.cos(yaw),
                    "y": y + d * math.sin(yaw),
                    "z": 0.5,
                }
            )
        await websocket.send(
            envelope(
                "trajectory",
                stamp,
                "map",
                {
                    "stamp_ns": stamp,
                    "frame_id": "map",
                    "points": traj_pts,
                },
            )
        )

        objects = []
        for idx, (dx, dy, label, length, width) in enumerate(
            [
                (18.0, 6.0, "vehicle", 4.5, 2.0),
                (-12.0, 14.0, "vehicle", 4.2, 1.9),
                (8.0, -16.0, "pedestrian", 0.6, 0.6),
                (22.0, -8.0, "cyclist", 1.8, 0.7),
            ]
        ):
            c, s = math.cos(yaw), math.sin(yaw)
            rx = dx * c - dy * s
            ry = dx * s + dy * c
            objects.append(
                {
                    "object_id": f"obj_{idx}",
                    "classification": label,
                    "x": x + rx,
                    "y": y + ry,
                    "z": 0.5,
                    "length_m": length,
                    "width_m": width,
                    "height_m": 1.5,
                }
            )
        await websocket.send(
            envelope(
                "perception_objects",
                stamp,
                "map",
                {"stamp_ns": stamp, "frame_id": "map", "objects": objects},
            )
        )

        cam_b64 = make_front_camera_b64(t)
        if cam_b64:
            await websocket.send(
                envelope(
                    "front_camera",
                    stamp,
                    "carla_rgb_front",
                    {
                        "encoding": "base64",
                        "format": "jpeg",
                        "width": 1280,
                        "height": 720,
                        "payload": cam_b64,
                    },
                )
            )

        await asyncio.sleep(0.2)


async def main() -> None:
    try:
        import websockets
    except ImportError:
        raise SystemExit("pip install websockets") from None

    async with websockets.serve(client_handler, HOST, PORT):
        print(f"Mock telemetry WS on ws://{HOST}:{PORT} (Ctrl+C to stop)")
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
