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
from typing import Any, Optional, Set

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
    find_vehicle_by_car_id,
    publish_carla_live_speed,
    publish_sdv_fleet_telemetry,
    read_speed_limit_kmh,
    scan_dynamic_actors,
    scan_traffic_lights,
    snapshot_client_handler,
    telemetry_client_handler,
)
from pygame_vehicle_sprites import patch_no_rendering_vehicles  # noqa: E402


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

_KOREAN_FONT_PATHS = (
    Path(r"C:\Windows\Fonts\malgun.ttf"),
    Path(r"C:\Windows\Fonts\malgunbd.ttf"),
    Path("/usr/share/fonts/truetype/nanum/NanumGothic.ttf"),
    Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
    Path("/System/Library/Fonts/Supplemental/AppleGothic.ttf"),
)
_UI_FONT_CACHE: dict[int, Any] = {}


def _korean_font(size: int) -> Any:
    cached = _UI_FONT_CACHE.get(size)
    if cached is not None:
        return cached
    for path in _KOREAN_FONT_PATHS:
        if path.is_file():
            font = pygame.font.Font(str(path), size)
            _UI_FONT_CACHE[size] = font
            return font
    try:
        font = pygame.font.SysFont(
            "malgungothic,nanumgothic,applegothic,notosanscjkkr,sans",
            size,
        )
    except Exception:
        font = pygame.font.Font(pygame.font.get_default_font(), size)
    _UI_FONT_CACHE[size] = font
    return font


def _wrap_ui_text(text: str, font: Any, max_width: int) -> list[str]:
    raw = str(text or "").strip()
    if not raw:
        return [""]
    lines: list[str] = []
    for paragraph in raw.splitlines():
        paragraph = paragraph.strip()
        if not paragraph:
            lines.append("")
            continue
        current = ""
        for ch in paragraph:
            if ch == " " and current and font.size(current + " ")[0] > max_width:
                lines.append(current)
                current = ""
                continue
            trial = current + ch
            if font.size(trial)[0] <= max_width:
                current = trial
            else:
                if current:
                    lines.append(current)
                current = ch
        if current:
            lines.append(current)
    return lines or [""]


def _focus_pygame_window() -> None:
    if os.name != "nt":
        return
    try:
        import ctypes

        info = pygame.display.get_wm_info()
        hwnd = info.get("window")
        if hwnd:
            ctypes.windll.user32.SetForegroundWindow(hwnd)
    except Exception:
        pass


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
        self._pg_world: Any = None
        self._pending_car_id: Optional[str] = None
        self._car_switch_lock = threading.Lock()
        self._promote_prompt_lock = threading.Lock()
        self._promote_prompt: Optional[dict[str, Any]] = None
        self._promote_prompt_ws: Any = None
        self._promote_btn_yes: Optional[Any] = None
        self._promote_btn_no: Optional[Any] = None
        self._display_surface: Any = None
        self._display_hidden_flag = pygame.HIDDEN if hasattr(pygame, "HIDDEN") else 0
        self._prompt_window_visible = False
        self.active_car_id = "car_1"
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

    def set_promote_prompt(self, msg: dict[str, Any], websocket: Any) -> None:
        with self._promote_prompt_lock:
            self._promote_prompt = {
                "request_id": str(msg.get("request_id") or ""),
                "feature": str(msg.get("feature") or ""),
                "version": str(msg.get("version") or ""),
                "message": str(msg.get("message") or "").strip(),
                "detail": str(msg.get("detail") or "").strip(),
            }
            self._promote_prompt_ws = websocket
        print(
            "Promote Primary confirm — pygame 창에서 예(Y/Enter) / 아니오(N/Esc) 또는 버튼 클릭"
        )

    def answer_promote_prompt(self, accepted: bool) -> None:
        """Thread-safe entry for WS handler (async thread) and pygame input."""
        if not self._has_promote_prompt():
            return
        self._answer_promote_prompt(accepted)

    def _show_prompt_window(self) -> None:
        if self._prompt_window_visible:
            return
        try:
            pygame.display.set_caption(
                "SDV Primary 확인 — 예(Y/Enter) · 아니오(N/Esc) · 버튼 클릭"
            )
            self._display_surface = pygame.display.set_mode(
                (self.width, self.height), 0
            )
            self._prompt_window_visible = True
            _focus_pygame_window()
        except Exception as exc:
            print(f"show prompt window failed: {exc}")

    def _hide_prompt_window(self) -> None:
        if not self._prompt_window_visible:
            return
        try:
            self._display_surface = pygame.display.set_mode(
                (self.width, self.height), self._display_hidden_flag
            )
            pygame.display.set_caption("SDV CARLA pygame map")
            self._prompt_window_visible = False
        except Exception as exc:
            print(f"hide prompt window failed: {exc}")

    def clear_promote_prompt(self) -> None:
        with self._promote_prompt_lock:
            self._promote_prompt = None
            self._promote_prompt_ws = None
            self._promote_btn_yes = None
            self._promote_btn_no = None

    def _get_promote_hit_rects(self) -> tuple[Optional[Any], Optional[Any]]:
        with self._promote_prompt_lock:
            return self._promote_btn_yes, self._promote_btn_no

    def _has_promote_prompt(self) -> bool:
        with self._promote_prompt_lock:
            return self._promote_prompt is not None

    def _answer_promote_prompt(self, accepted: bool) -> None:
        with self._promote_prompt_lock:
            prompt = dict(self._promote_prompt) if self._promote_prompt else None
            ws = self._promote_prompt_ws
            self._promote_prompt = None
            self._promote_prompt_ws = None
            self._promote_btn_yes = None
            self._promote_btn_no = None
        if not prompt or ws is None or not self._loop:
            return
        payload = json.dumps({
            "type": "promote_primary_response",
            "request_id": prompt.get("request_id", ""),
            "accepted": accepted,
        })

        async def _send() -> None:
            try:
                await ws.send(payload)
            except Exception as exc:
                print(f"promote_primary_response send failed: {exc}")

        try:
            if self._loop and self._loop.is_running():
                asyncio.run_coroutine_threadsafe(_send(), self._loop)
            else:
                print("promote_primary_response skipped: asyncio loop not running")
        except Exception as exc:
            print(f"promote_primary_response schedule failed: {exc}")
        label = "accepted" if accepted else "declined"
        print(f"Promote Primary confirm {label} in pygame cockpit")

    def _render_promote_overlay(self, display: pygame.Surface, nrm: Any) -> None:
        with self._promote_prompt_lock:
            prompt = dict(self._promote_prompt) if self._promote_prompt else None
        if not prompt:
            if self._prompt_window_visible:
                self._hide_prompt_window()
            self._promote_btn_yes = None
            self._promote_btn_no = None
            return

        if not self._prompt_window_visible:
            self._show_prompt_window()
            display = self._display_surface or display

        w, h = self.width, self.height
        overlay = pygame.Surface((w, h), pygame.SRCALPHA)
        overlay.fill((0, 0, 0, 170))
        display.blit(overlay, (0, 0))

        panel_w = min(760, w - 80)
        panel_h = min(360, h - 120)
        panel_x = (w - panel_w) // 2
        panel_y = (h - panel_h) // 2
        panel = pygame.Rect(panel_x, panel_y, panel_w, panel_h)
        pygame.draw.rect(display, nrm.COLOR_ALUMINIUM_5, panel)
        border = getattr(nrm, "COLOR_BUTTER_0", getattr(nrm, "COLOR_YELLOW_2", (252, 233, 79)))
        pygame.draw.rect(display, border, panel, 3)

        title_font = _korean_font(26)
        body_font = _korean_font(22)
        hint_font = _korean_font(18)
        btn_font = _korean_font(20)

        title = title_font.render("Primary 업데이트 확인", True, nrm.COLOR_WHITE)
        display.blit(title, title.get_rect(centerx=panel.centerx, top=panel_y + 24))

        feature = prompt.get("feature") or ""
        version = prompt.get("version") or ""
        default_msg = (
            f"{feature} feature의 새 버전을 Primary로 업데이트하시겠습니까?"
            if feature
            else "새 버전을 Primary로 업데이트하시겠습니까?"
        )
        message = prompt.get("message") or default_msg
        y = panel_y + 72
        for line in _wrap_ui_text(message, body_font, panel_w - 48):
            if not line:
                y += body_font.get_linesize() // 2
                continue
            surf = body_font.render(line, True, nrm.COLOR_WHITE)
            display.blit(surf, (panel_x + 24, y))
            y += surf.get_height() + 6

        detail = prompt.get("detail") or ""
        if not detail and version:
            detail = f"Feature: {feature}\n버전: {version}" if feature else f"버전: {version}"
        if detail:
            y += 8
            for line in detail.splitlines():
                line = line.strip()
                if not line:
                    continue
                surf = body_font.render(line, True, nrm.COLOR_ALUMINIUM_1)
                display.blit(surf, (panel_x + 24, y))
                y += surf.get_height() + 4

        btn_w, btn_h, gap = 160, 44, 24
        total_w = btn_w * 2 + gap
        btn_y = panel.bottom - btn_h - 28
        btn_x0 = panel.centerx - total_w // 2
        yes_rect = pygame.Rect(btn_x0, btn_y, btn_w, btn_h)
        no_rect = pygame.Rect(btn_x0 + btn_w + gap, btn_y, btn_w, btn_h)
        with self._promote_prompt_lock:
            self._promote_btn_yes = yes_rect
            self._promote_btn_no = no_rect

        yes_color = getattr(nrm, "COLOR_CHAMELEON_0", getattr(nrm, "COLOR_GREEN", (138, 226, 52)))
        no_color = getattr(nrm, "COLOR_SCARLET_RED_0", getattr(nrm, "COLOR_RED", (239, 41, 41)))
        pygame.draw.rect(display, yes_color, yes_rect)
        pygame.draw.rect(display, no_color, no_rect)
        yes_label = btn_font.render("예 (Y)", True, nrm.COLOR_WHITE)
        no_label = btn_font.render("아니오 (N)", True, nrm.COLOR_WHITE)
        display.blit(yes_label, yes_label.get_rect(center=yes_rect.center))
        display.blit(no_label, no_label.get_rect(center=no_rect.center))

        hint = hint_font.render(
            "키보드: Y / Enter = 예   ·   N / Esc = 아니오",
            True,
            nrm.COLOR_ALUMINIUM_1,
        )
        display.blit(hint, hint.get_rect(centerx=panel.centerx, bottom=panel.bottom - 8))

    @staticmethod
    def _wrap_text(text: str, font: Any, max_width: int) -> list[str]:
        return _wrap_ui_text(text, font, max_width)

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
            (self.width, self.height), self._display_hidden_flag
        )
        self._display_surface = display
        patch_no_rendering_vehicles(nrm)

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
        self._pg_world = pg_world
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
                    continue
                if event.type == pygame.KEYUP and input_control._is_quit_shortcut(event.key):
                    session._running = False
                    continue
                if not session._has_promote_prompt():
                    continue
                if event.type in (pygame.KEYDOWN, pygame.KEYUP):
                    if event.type != pygame.KEYDOWN:
                        continue
                    key = event.key
                    if key in (pygame.K_y, pygame.K_RETURN, pygame.K_KP_ENTER):
                        session.answer_promote_prompt(True)
                    elif key in (pygame.K_n, pygame.K_ESCAPE):
                        session.answer_promote_prompt(False)
                elif event.type in (pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP):
                    if event.type != pygame.MOUSEBUTTONDOWN or event.button not in (1,):
                        continue
                    yes_rect, no_rect = session._get_promote_hit_rects()
                    pos = event.pos
                    if yes_rect and yes_rect.collidepoint(pos):
                        session.answer_promote_prompt(True)
                    elif no_rect and no_rect.collidepoint(pos):
                        session.answer_promote_prompt(False)

        input_control._parse_keys = _parse_keys_from_dashboard
        input_control._parse_events = _parse_events_minimal

        def _parse_input_dashboard_only(clock: Any) -> None:
            """Map pan/zoom + sync dashboard gear/throttle into pygame HUD (no local keyboard drive)."""
            input_control._parse_events()
            if not session._has_promote_prompt():
                input_control._parse_mouse()
            if session.controller is not None and isinstance(input_control.control, carla.VehicleControl):
                _parse_keys_from_dashboard(clock.get_time())

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
                session._apply_fleet_vehicle_switch()
                clock.tick_busy_loop(60)
                if session.autonomy:
                    session.autonomy.tick()
                pg_world.tick(clock)
                hud.tick(clock)
                input_control.tick(clock)
                # Apply dashboard WS control last (same slot as no_rendering_mode InputControl).
                if session.controller:
                    session.controller.tick()

                display = session._display_surface or display
                display.fill(nrm.COLOR_ALUMINIUM_4)
                pg_world.render(display)
                hud.render(display)
                input_control.render(display)
                session._render_promote_overlay(display, nrm)
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

    def request_fleet_vehicle(self, car_id: str) -> None:
        target = str(car_id or "").strip()
        if not target:
            return
        with self._car_switch_lock:
            self._pending_car_id = target

    def _apply_fleet_vehicle_switch(self) -> None:
        with self._car_switch_lock:
            car_id = self._pending_car_id
            self._pending_car_id = None
        if not car_id or car_id == self.active_car_id or not self.world:
            return
        vehicle = find_vehicle_by_car_id(self.world, car_id)
        if vehicle is None:
            print(f"select_vehicle: {car_id} not in CARLA — run spawn_sdv_fleet.py first")
            return
        old = self.vehicle
        if old is not None and old.id != vehicle.id:
            try:
                old.set_autopilot(True)
            except RuntimeError:
                pass
        try:
            vehicle.set_autopilot(False)
        except RuntimeError:
            pass
        self.vehicle = vehicle
        self.active_car_id = car_id
        if self.controller is not None:
            self.controller.vehicle = vehicle
            self.controller.emergency = False
            self.controller.autopilot = False
            self.controller.throttle = 0.0
            self.controller.brake = 0.0
            self.controller.steer = 0.0
        if self.autonomy is not None:
            self.autonomy.vehicle = vehicle
            self.autonomy.stop()
        if self._pg_world is not None:
            self._pg_world.hero_actor = vehicle
            self._pg_world.hero_transform = vehicle.get_transform()
        print(f"CARLA cockpit switched to {car_id} (actor id={vehicle.id})")

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
        publish_carla_live_speed(speed, car_id=getattr(self, "active_car_id", "car_1"))
        publish_sdv_fleet_telemetry(self.world)
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
