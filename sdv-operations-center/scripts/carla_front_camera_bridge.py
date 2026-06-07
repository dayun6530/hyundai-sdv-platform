#!/usr/bin/env python3
"""
Publish CARLA 0.9.15 front RGB camera to sdv_ros_bridge WebSocket as `front_camera` envelopes.

Requires: carla Python API (matching CARLA 0.9.15), websockets, pillow

  pip install websockets pillow
  python carla_front_camera_bridge.py --host 127.0.0.1 --port 2000 --ws ws://127.0.0.1:9090
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import io
import json
import time

try:
    import carla
except ImportError as exc:
    raise SystemExit("Install CARLA 0.9.15 Python API (carla package)") from exc


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


def bgr_to_jpeg_b64(carla_image) -> str:
    from PIL import Image

    array = carla_image.raw_data
    img = Image.frombytes("RGBA", (carla_image.width, carla_image.height), array)
    rgb = img.convert("RGB")
    buf = io.BytesIO()
    rgb.save(buf, format="JPEG", quality=88)
    return base64.b64encode(buf.getvalue()).decode("ascii")


async def stream_camera(ws_url: str, host: str, port: int, role_name: str) -> None:
    import websockets

    client = carla.Client(host, port)
    client.set_timeout(10.0)
    world = client.get_world()

    blueprint_library = world.get_blueprint_library()
    vehicle_bp = blueprint_library.filter(role_name)[0]
    spawn_points = world.get_map().get_spawn_points()
    vehicle = world.spawn_actor(vehicle_bp, spawn_points[0])

    cam_bp = blueprint_library.find("sensor.camera.rgb")
    cam_bp.set_attribute("image_size_x", "1280")
    cam_bp.set_attribute("image_size_y", "720")
    cam_bp.set_attribute("fov", "90")
    cam_transform = carla.Transform(carla.Location(x=1.6, z=1.45))
    camera = world.spawn_actor(cam_bp, cam_transform, attach_to=vehicle)

    latest: dict[str, str | int] = {}

    def on_image(image: carla.Image) -> None:
        image.convert(carla.ColorConverterType.Raw)
        latest["payload"] = bgr_to_jpeg_b64(image)
        latest["width"] = image.width
        latest["height"] = image.height

    camera.listen(on_image)
    vehicle.set_autopilot(True)

    async with websockets.connect(ws_url) as ws:
        print(f"Streaming CARLA front_camera → {ws_url}")
        while True:
            if "payload" in latest:
                stamp = time.time_ns()
                await ws.send(
                    envelope(
                        "front_camera",
                        stamp,
                        "carla_rgb_front",
                        {
                            "encoding": "base64",
                            "format": "jpeg",
                            "width": latest.get("width", 1280),
                            "height": latest.get("height", 720),
                            "payload": latest["payload"],
                        },
                    )
                )
            await asyncio.sleep(0.05)

    camera.stop()
    camera.destroy()
    vehicle.destroy()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2000)
    parser.add_argument("--ws", default="ws://127.0.0.1:9090")
    parser.add_argument("--role", default="vehicle.tesla.model3")
    args = parser.parse_args()
    asyncio.run(stream_camera(args.ws, args.host, args.port, args.role))


if __name__ == "__main__":
    main()
