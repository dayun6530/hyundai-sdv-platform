#!/usr/bin/env python3
"""
Stream CARLA no_rendering_mode pygame view into SDV Dashboard cockpit (pixel-identical).

Uses the same game loop as PythonAPI/examples/no_rendering_mode.py:
  world.tick → hud.tick → input (dashboard WASD) → render → flip → JPEG → :9090

Launch:
  python pygame_map_bridge.py
  SDV_Dashboard.exe --carla-mode bridge
"""
from __future__ import annotations

try:
    import websockets  # noqa: F401
except ImportError as exc:
    raise SystemExit(
        "pip install -r requirements-bridge.txt\n"
        "(websockets pillow pygame)"
    ) from exc

import argparse
import asyncio
import base64
import glob
import importlib.util
import io
import json
import math
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Set

try:
    import pygame
except ImportError as exc:
    raise SystemExit("pip install pygame") from exc

try:
    import carla
except ImportError as exc:
    raise SystemExit("Install CARLA 0.9.15 Python API") from exc

from carla_integration_bridge import (  # noqa: E402
    HOST_CONTROL,
    HOST_SNAPSHOT,
    HOST_TELEMETRY,
    PORT_CONTROL,
    PORT_SNAPSHOT,
    PORT_TELEMETRY,
    CarlaAutonomyManager,
    CarlaVehicleController,
    control_client_handler,
    envelope,
    read_speed_limit_kmh,
    scan_dynamic_actors,
    scan_traffic_lights,
    snapshot_client_handler,
    telemetry_client_handler,
)


def _port_in_use(host: str, port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.25)
        return sock.connect_ex((host, port)) == 0


def _pid_listening_on_port(port: int) -> str:
    if os.name != "nt":
        return ""
    try:
        out = subprocess.check_output(
            ["netstat", "-ano"], stderr=subprocess.DEVNULL, universal_newlines=True
        )
    except (OSError, subprocess.CalledProcessError):
        return ""
    needle = f":{port}"
    for line in out.splitlines():
        if needle not in line or "LISTENING" not in line.upper():
            continue
        parts = line.split()
        if parts:
            return parts[-1]
    return ""


def _require_ports_free(host: str, ports: list) -> None:
    busy = [p for p in ports if _port_in_use(host, p)]
    if not busy:
        return
    lines = [
        "Bridge ports already in use (only one bridge at a time):",
        *(f"  {host}:{p}" + (f"  PID {_pid_listening_on_port(p)}" if _pid_listening_on_port(p) else "")
          for p in busy),
        "",
        "Another process is probably:",
        "  carla_integration_bridge.py, mock_telemetry_ws_server.py, or a previous pygame_map_bridge.py",
        "",
        "Fix (PowerShell, from scripts/):",
        "  .\\stop_bridge_ports.ps1",
        "  # or: taskkill /PID <pid> /F",
    ]
    raise SystemExit("\n".join(lines))


def _carla_root() -> Path:
    env = os.environ.get("CARLA_ROOT", "").strip()
    if env:
        root = Path(env)
        if root.is_dir():
            return root.resolve()
    default = Path(r"C:\Users\82107\desktop\CARLA_0.9.15\WindowsNoEditor")
    if default.is_dir():
        return default.resolve()
    raise SystemExit("Set CARLA_ROOT to WindowsNoEditor")


def _bootstrap_carla_python(root: Path) -> Path:
    examples = root / "PythonAPI" / "examples"
    carla_dir = root / "PythonAPI" / "carla"
    agents_dir = carla_dir / "agents"
    for path in (agents_dir, carla_dir, examples):
        s = str(path)
        if path.is_dir() and s not in sys.path:
            sys.path.insert(0, s)
    egg_glob = str(carla_dir / "dist" / "carla-*%d.%d-%s.egg") % (
        sys.version_info.major,
        sys.version_info.minor,
        "win-amd64" if os.name == "nt" else "linux-x86_64",
    )
    for egg in glob.glob(egg_glob):
        if egg not in sys.path:
            sys.path.insert(0, egg)
    script = examples / "no_rendering_mode.py"
    if not script.is_file():
        raise SystemExit(f"Missing {script}")
    return script


def _load_no_rendering_module(script: Path):
    spec = importlib.util.spec_from_file_location("carla_no_rendering_mode", script)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Cannot load {script}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


CAMERA_FPS = 15
CAMERA_JPEG_QUALITY = 82


def _surface_to_jpeg_b64(surface: pygame.Surface, quality: int = CAMERA_JPEG_QUALITY) -> str:
    buf = io.BytesIO()
    try:
        pygame.image.save(surface, buf, "JPEG", quality=quality)
    except Exception:
        buf = io.BytesIO()
        pygame.image.save(surface, buf, "PNG")
    data = buf.getvalue()
    return base64.b64encode(data).decode("ascii") if data else ""


class PygameMapSession:
    """Same renderer as no_rendering_mode.py; frames pushed to Dashboard."""

    def __init__(
        self,
        host: str,
        port: int,
        *,
        width: int = 1280,
        height: int = 720,
        vehicle_filter: str = "vehicle.*",
        no_rendering: bool = False,
        asyncio_loop: asyncio.AbstractEventLoop | None = None,
    ) -> None:
        self._lock = threading.Lock()
        self._running = True
        self._latest_image: dict[str, Any] = {}
        self._ready = threading.Event()
        self._loop = asyncio_loop
        self._stream_clients: Set[Any] = set()
        self._clients_lock = threading.Lock()

        self.width = width
        self.height = height
        self.vehicle: carla.Vehicle | None = None
        self.world: carla.World | None = None
        self.controller: CarlaVehicleController | None = None
        self.autonomy: CarlaAutonomyManager | None = None
        # Interface expected by carla_integration_bridge handlers
        self.active_camera = "pygame_map"
        self.latest_gnss: dict[str, float] = {}
        self.latest_imu: dict[str, float] = {"accel_x_mps2": 0.0}

        self._thread = threading.Thread(
            target=self._game_loop_thread,
            args=(host, port, vehicle_filter, no_rendering),
            name="carla-no-rendering-loop",
            daemon=True,
        )
        self._thread.start()
        if not self._ready.wait(timeout=90.0):
            raise RuntimeError("CARLA pygame world did not start in 90s")

        print(
            f"Connected — pygame map {width}x{height} → Dashboard front_camera @ {CAMERA_FPS} FPS"
        )
        print(f"  ws://{HOST_TELEMETRY}:{PORT_TELEMETRY}  control ws://{HOST_CONTROL}:{PORT_CONTROL}")

    def register_stream_client(self, ws: Any) -> None:
        with self._clients_lock:
            self._stream_clients.add(ws)

    def unregister_stream_client(self, ws: Any) -> None:
        with self._clients_lock:
            self._stream_clients.discard(ws)

    def _store_latest_frame(self, payload_b64: str) -> None:
        if not payload_b64:
            return
        with self._lock:
            self._latest_image["payload_b64"] = payload_b64

    async def _broadcast_camera(self, payload_b64: str) -> None:
        stamp = time.time_ns()
        text = envelope(
            "front_camera",
            stamp,
            "pygame_map",
            {
                "encoding": "base64",
                "format": "jpeg",
                "width": self.width,
                "height": self.height,
                "payload": payload_b64,
                "slot": "pygame_map",
                "source": "no_rendering_mode",
            },
        )
        dead = []
        with self._clients_lock:
            clients = list(self._stream_clients)
        for ws in clients:
            try:
                await ws.send(text)
            except Exception:
                dead.append(ws)
        if dead:
            with self._clients_lock:
                for ws in dead:
                    self._stream_clients.discard(ws)

    def _game_loop_thread(
        self, host: str, port: int, vehicle_filter: str, no_rendering: bool
    ) -> None:
        root = _carla_root()
        nrm = _load_no_rendering_module(_bootstrap_carla_python(root))

        os.environ.setdefault("SDL_VIDEODRIVER", "windib")
        pygame.init()
        display = pygame.display.set_mode(
            (self.width, self.height), pygame.HIDDEN if hasattr(pygame, "HIDDEN") else 0
        )

        args = SimpleNamespace(
            host=host,
            port=port,
            width=self.width,
            height=self.height,
            filter=vehicle_filter,
            map=None,
            no_rendering=no_rendering,
            show_triggers=False,
            show_connections=False,
            show_spawn_points=False,
            description="SDV Dashboard pygame map",
        )

        font = pygame.font.Font(pygame.font.get_default_font(), 20)
        text_surface = font.render("Rendering map...", True, nrm.COLOR_WHITE)
        display.fill(nrm.COLOR_ALUMINIUM_4)
        display.blit(text_surface, text_surface.get_rect(center=(self.width / 2, self.height / 2)))

        hud = nrm.HUD(nrm.TITLE_HUD, self.width, self.height)
        pg_world = nrm.World(nrm.TITLE_WORLD, args, timeout=30.0)
        input_control = nrm.InputControl(nrm.TITLE_INPUT)

        session = self

        def _parse_keys_from_dashboard(milliseconds: int) -> None:
            if session.controller is None or input_control.control is None:
                return
            c = input_control.control
            vc = session.controller
            if vc.autopilot or vc.emergency:
                return
            c.throttle = max(0.0, min(1.0, vc.throttle))
            c.brake = max(0.0, min(1.0, vc.brake))
            c.steer = max(-1.0, min(1.0, vc.steer))
            c.hand_brake = vc.hand_brake
            c.reverse = vc.reverse

        def _parse_events_minimal() -> None:
            pygame.event.pump()
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    session._running = False
                elif event.type == pygame.KEYUP and input_control._is_quit_shortcut(event.key):
                    session._running = False

        input_control._parse_keys = _parse_keys_from_dashboard
        input_control._parse_events = _parse_events_minimal

        def _parse_input_dashboard_only(clock: Any) -> None:
            """Map pan/zoom only — vehicle control comes from Dashboard WS (controller.tick)."""
            input_control._parse_events()
            input_control._parse_mouse()

        input_control.parse_input = _parse_input_dashboard_only

        input_control.start(hud, pg_world)
        hud.start()
        pg_world.start(hud, input_control)

        hero = pg_world.hero_actor
        if hero is None:
            raise RuntimeError("no hero vehicle")

        self.vehicle = hero
        self.world = pg_world.world
        self.controller = CarlaVehicleController(hero)
        self.autonomy = CarlaAutonomyManager(hero, self.world, self.world.get_map())
        self._ready.set()

        real_flip = pygame.display.flip

        def capturing_flip() -> None:
            surf = pygame.display.get_surface()
            if surf is not None:
                payload = _surface_to_jpeg_b64(surf)
                if payload:
                    with session._lock:
                        session._latest_image = {
                            "payload_b64": payload,
                            "width": session.width,
                            "height": session.height,
                        }
                    session._store_latest_frame(payload)
            try:
                real_flip()
            except Exception:
                pass

        pygame.display.flip = capturing_flip

        clock = pygame.time.Clock()
        try:
            while session._running:
                clock.tick_busy_loop(60)
                if session.autonomy:
                    session.autonomy.tick()
                if session.controller:
                    session.controller.tick()
                pg_world.tick(clock)
                hud.tick(clock)
                input_control.tick(clock)

                display.fill(nrm.COLOR_ALUMINIUM_4)
                pg_world.render(display)
                hud.render(display)
                input_control.render(display)
                pygame.display.flip()
        except Exception as exc:
            print(f"pygame loop ended: {exc}")
        finally:
            pygame.display.flip = real_flip
            try:
                pg_world.destroy()
            except Exception:
                pass
            pygame.quit()

    def set_active_camera(self, name: str) -> None:
        if name in ("pygame_map", "front", "hero", "map"):
            self.active_camera = "pygame_map"

    def active_image(self) -> dict[str, Any]:
        with self._lock:
            return dict(self._latest_image)

    def collect(self) -> tuple[int, dict, list[dict], list[dict], dict]:
        if not self.vehicle or not self.controller or not self.autonomy:
            raise RuntimeError("session not ready")
        stamp = time.time_ns()
        tf = self.vehicle.get_transform()
        vel = self.vehicle.get_velocity()
        speed = math.hypot(vel.x, vel.y)
        control = self.vehicle.get_control()
        yaw = math.radians(tf.rotation.yaw)

        vehicle = {
            "frame_id": "map",
            "position": {"x": tf.location.x, "y": tf.location.y, "z": tf.location.z},
            "yaw_rad": yaw,
            "speed_mps": speed,
            "longitudinal_velocity_mps": speed,
            "lateral_velocity_mps": 0.0,
            "heading_rate_rps": 0.0,
            "steering_angle_deg": control.steer * 70.0,
            "throttle": control.throttle,
            "brake": control.brake,
            "gear": 2 if control.reverse else (3 if speed > 0.5 else 1),
            "adas_active": self.controller.autopilot,
            "nav_arrow": self.controller.nav_arrow,
            "nav_remain_m": self.controller.nav_remain_m,
        }

        ego_loc = self.vehicle.get_location()
        try:
            geo = self.world.get_map().transform_to_geolocation(ego_loc)
            self.latest_gnss = {
                "latitude_deg": geo.latitude,
                "longitude_deg": geo.longitude,
                "altitude_m": geo.altitude,
            }
        except Exception:
            pass
        actors = scan_dynamic_actors(self.world, self.vehicle, tf)
        lights = scan_traffic_lights(self.world, ego_loc)
        speed_kmh = read_speed_limit_kmh(self.vehicle, self.world)
        traj_pts = self.controller.route_points()
        progress = self.controller.route_progress()
        eta = self.controller.eta_sec(speed)
        self.controller.autonomy_maneuver = self.autonomy.maneuver
        twin = {
            "speed_limit_kmh": speed_kmh,
            "traffic_lights": lights,
            "navigation": {
                "arrow": self.controller.nav_arrow,
                "remain_m": round(self.controller.nav_remain_m, 1),
                "eta_sec": round(eta, 1),
                "progress": round(progress, 3),
                "maneuver": self.autonomy.maneuver,
            },
            "autonomy": {
                "mode": self.autonomy.mode,
                "state": self.autonomy.state,
                "maneuver": self.autonomy.maneuver,
            },
        }
        return stamp, vehicle, actors, traj_pts, twin

    def destroy(self) -> None:
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=5.0)


async def _camera_pump(session: PygameMapSession) -> None:
    """Send latest pygame frame at CAMERA_FPS (game loop still runs at 60)."""
    interval = 1.0 / float(CAMERA_FPS)
    while session._running:
        await asyncio.sleep(interval)
        with session._lock:
            payload = session._latest_image.get("payload_b64", "")
        if payload:
            await session._broadcast_camera(payload)


async def telemetry_with_camera(ws: Any, session: PygameMapSession) -> None:
    session.register_stream_client(ws)
    try:
        await telemetry_client_handler(ws, session)
    finally:
        session.unregister_stream_client(ws)


async def run_servers(
    host: str,
    port: int,
    snapshot_port: int,
    control_port: int,
    *,
    width: int,
    height: int,
    vehicle_filter: str,
    no_rendering: bool,
) -> None:
    import websockets

    _require_ports_free(HOST_TELEMETRY, [PORT_TELEMETRY, snapshot_port, control_port])

    loop = asyncio.get_running_loop()
    session = PygameMapSession(
        host,
        port,
        width=width,
        height=height,
        vehicle_filter=vehicle_filter,
        no_rendering=no_rendering,
        asyncio_loop=loop,
    )

    async def handler_telemetry(ws):
        await telemetry_with_camera(ws, session)

    async def handler_snapshot(ws):
        await snapshot_client_handler(ws, session)

    async def handler_control(ws):
        await control_client_handler(ws, session)

    pump = asyncio.create_task(_camera_pump(session))
    async with websockets.serve(handler_telemetry, HOST_TELEMETRY, PORT_TELEMETRY):
        async with websockets.serve(handler_snapshot, HOST_SNAPSHOT, snapshot_port):
            async with websockets.serve(handler_control, HOST_CONTROL, control_port):
                print("Dashboard: 1st Person cockpit = pygame no_rendering_mode (overlays off)")
                try:
                    await asyncio.Future()
                finally:
                    session.destroy()
                    pump.cancel()


def main() -> None:
    parser = argparse.ArgumentParser(description="pygame no_rendering_mode → SDV Dashboard")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("-p", "--port", type=int, default=2000)
    parser.add_argument("--snapshot-port", type=int, default=PORT_SNAPSHOT)
    parser.add_argument("--control-port", type=int, default=PORT_CONTROL)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--filter", default="vehicle.*", dest="vehicle_filter")
    parser.add_argument("--no-rendering", action="store_true")
    args = parser.parse_args()
    asyncio.run(
        run_servers(
            args.host,
            args.port,
            args.snapshot_port,
            args.control_port,
            width=args.width,
            height=args.height,
            vehicle_filter=args.vehicle_filter,
            no_rendering=args.no_rendering,
        )
    )


if __name__ == "__main__":
    main()
