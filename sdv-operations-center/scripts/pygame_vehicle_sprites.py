"""
Replace CARLA no_rendering_mode polygon vehicles with PNG top-down sprites.

Fleet model mapping comes from config/fleet/vehicles.json (car_id -> model).

Drop PNG files into sdv-operations-center/assets/vehicles/:
  ioniq5.png    — IONIQ5
  ioniq6.png    — IONIQ6
  kona_ev.png   — KONA_EV
  traffic.png   — non-fleet traffic (optional fallback)
  motorcycle.png — 2-wheel vehicles (optional)

Image front = top of PNG, transparent background.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Callable, Optional

try:
    import pygame
except ImportError:
    pygame = None  # type: ignore

MODEL_SPRITE_FILES = {
    "IONIQ5": "ioniq5.png",
    "IONIQ6": "ioniq6.png",
    "KONA_EV": "kona_ev.png",
}

EXTRA_SPRITE_FILES = {
    "traffic": "traffic.png",
    "motorcycle": "motorcycle.png",
}

DEFAULT_ASSETS_DIR = Path(__file__).resolve().parent.parent / "assets" / "vehicles"
REPO_ROOT = Path(__file__).resolve().parent.parent.parent

FLEET_CONFIG_CANDIDATES = (
    REPO_ROOT / "sdv-feature-flag-platform" / "config" / "fleet" / "vehicles.json",
    REPO_ROOT / "sdv-ota-manager" / "platform" / "config" / "fleet" / "vehicles.json",
)


def _car_id_from_role(role: str) -> Optional[str]:
    if role == "hero":
        return "car_1"
    if role.startswith("sdv_car_"):
        return role.replace("sdv_", "", 1)
    return None


def _normalize_model(raw: str) -> Optional[str]:
    key = str(raw or "").strip().upper().replace("-", "_").replace(" ", "_")
    aliases = {
        "IONIQ_5": "IONIQ5",
        "IONIQ5": "IONIQ5",
        "IONIQ_6": "IONIQ6",
        "IONIQ6": "IONIQ6",
        "KONA": "KONA_EV",
        "KONAEV": "KONA_EV",
        "KONA_EV": "KONA_EV",
    }
    return aliases.get(key)


def load_fleet_model_map(config_path: Optional[Path] = None) -> dict[str, str]:
    """car_id -> model name (IONIQ5 | IONIQ6 | KONA_EV)."""
    paths: list[Path] = []
    if config_path is not None:
        paths.append(Path(config_path))
    paths.extend(FLEET_CONFIG_CANDIDATES)

    for path in paths:
        if not path.is_file():
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            print(f"Vehicle sprites: failed to read fleet config {path}: {exc}")
            continue

        out: dict[str, str] = {}
        for entry in data.get("vehicles", []):
            car_id = str(entry.get("car_id", "")).strip()
            model = _normalize_model(str(entry.get("model", "")))
            if car_id and model:
                out[car_id] = model
        if out:
            print(f"Vehicle sprites: fleet models loaded from {path}")
            return out

    print("Vehicle sprites: fleet config not found — SDV cars use traffic fallback until vehicles.json is available")
    return {}


def _fallback_color(kind: str) -> tuple[int, int, int, int]:
    colors = {
        "IONIQ5": (52, 211, 153, 255),
        "IONIQ6": (56, 189, 248, 255),
        "KONA_EV": (251, 191, 36, 255),
        "traffic": (148, 163, 184, 255),
        "motorcycle": (244, 114, 182, 255),
    }
    return colors.get(kind, colors["traffic"])


def _make_fallback_sprite(kind: str, size: tuple[int, int] = (48, 96)) -> "pygame.Surface":
    w, h = size
    surf = pygame.Surface((w, h), pygame.SRCALPHA)
    fill = _fallback_color(kind)
    body = pygame.Rect(int(w * 0.14), int(h * 0.12), int(w * 0.72), int(h * 0.76))
    pygame.draw.rect(surf, fill, body, border_radius=max(2, w // 8))
    pygame.draw.rect(
        surf,
        (15, 23, 42, 210),
        pygame.Rect(int(w * 0.24), int(h * 0.24), int(w * 0.52), int(h * 0.22)),
        border_radius=max(1, w // 12),
    )
    pygame.draw.polygon(
        surf,
        (248, 250, 252, 230),
        [(w // 2, int(h * 0.06)), (int(w * 0.34), int(h * 0.18)), (int(w * 0.66), int(h * 0.18))],
    )
    return surf


class VehicleSpritePack:
    def __init__(
        self,
        assets_dir: Optional[Path] = None,
        fleet_config_path: Optional[Path] = None,
    ) -> None:
        if pygame is None:
            raise RuntimeError("pygame is required for vehicle sprites")
        self.assets_dir = Path(assets_dir or DEFAULT_ASSETS_DIR)
        self.assets_dir.mkdir(parents=True, exist_ok=True)
        self.fleet_models = load_fleet_model_map(fleet_config_path)
        self._sprites: dict[str, pygame.Surface] = {}
        self.using_fallback: set[str] = set()

        for model, filename in MODEL_SPRITE_FILES.items():
            self._load_sprite(model, filename)
        for kind, filename in EXTRA_SPRITE_FILES.items():
            self._load_sprite(kind, filename)

        if self.using_fallback:
            missing = sorted(self.using_fallback)
            print(
                "Vehicle sprites: using built-in placeholders for "
                f"[{', '.join(missing)}]. "
                f"Add PNG top-down sprites to {self.assets_dir}"
            )
        else:
            print(f"Vehicle sprites loaded from {self.assets_dir}")

    def _load_sprite(self, key: str, filename: str) -> None:
        path = self.assets_dir / filename
        if path.is_file():
            surf = pygame.image.load(str(path))
            if pygame.display.get_surface() is not None:
                surf = surf.convert_alpha()
            self._sprites[key] = surf
        else:
            self._sprites[key] = _make_fallback_sprite(key)
            self.using_fallback.add(filename)

    def _model_for_actor(self, actor: Any) -> Optional[str]:
        car_id = _car_id_from_role(str(actor.attributes.get("role_name", "")))
        if car_id:
            return self.fleet_models.get(car_id)
        return None

    def pick(self, actor: Any) -> pygame.Surface:
        model = self._model_for_actor(actor)
        if model and model in self._sprites:
            return self._sprites[model]

        wheels = int(actor.attributes.get("number_of_wheels", 4))
        if wheels == 2:
            return self._sprites["motorcycle"]
        return self._sprites["traffic"]

    def render_vehicle(
        self,
        surface: "pygame.Surface",
        actor: Any,
        transform: Any,
        world_to_pixel: Callable[[Any], list[int]],
        map_image: Any,
    ) -> None:
        sprite = self.pick(actor)
        bb = actor.bounding_box.extent
        center = world_to_pixel(transform.location)
        ppm = float(getattr(map_image, "_pixels_per_meter", 1.0))
        scale = float(getattr(map_image, "scale", 1.0))

        length_px = max(10, int(2.0 * bb.x * ppm * scale))
        width_px = max(8, int(2.0 * bb.y * ppm * scale))
        scaled = pygame.transform.smoothscale(sprite, (width_px, length_px))
        angle = -float(transform.rotation.yaw) - 90.0
        rotated = pygame.transform.rotate(scaled, angle)
        rect = rotated.get_rect(center=(int(center[0]), int(center[1])))
        surface.blit(rotated, rect)


def patch_no_rendering_vehicles(
    nrm_module: Any,
    assets_dir: Optional[Path] = None,
    fleet_config_path: Optional[Path] = None,
) -> VehicleSpritePack:
    """Monkey-patch World._render_vehicles to draw PNG sprites instead of polygons."""
    pack = VehicleSpritePack(assets_dir, fleet_config_path)

    def _render_vehicles_sprites(self, surface, list_v, world_to_pixel) -> None:
        for actor, transform in list_v:
            pack.render_vehicle(surface, actor, transform, world_to_pixel, self.map_image)

    nrm_module.World._render_vehicles = _render_vehicles_sprites
    return pack
