#!/usr/bin/env python3
"""
CARLA Control Center v2 — Hyundai SDV Operations Center

Telemetry (read):
  ws://127.0.0.1:9090  Phase-1 envelopes (dashboard RosBridgeClient + camera)
  ws://127.0.0.1:9092  Raw carla_snapshot (optional C++ CarlaWebSocketAdapter)

Control (write):
  ws://127.0.0.1:9093  carla_control commands from dashboard

Usage:
  python carla_integration_bridge.py --host 127.0.0.1 --port 2000
  SDV_Dashboard.exe --carla-mode bridge
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import io
import json
import math
import random
import time
from typing import Any

try:
    import carla
except ImportError as exc:
    raise SystemExit("Install CARLA 0.9.15 Python API (carla package)") from exc

try:
    from agents.navigation.global_route_planner import GlobalRoutePlanner
    from agents.navigation.basic_agent import BasicAgent
    from agents.navigation.behavior_agent import BehaviorAgent
except ImportError:
    GlobalRoutePlanner = None  # type: ignore
    BasicAgent = None  # type: ignore
    BehaviorAgent = None  # type: ignore

HOST_TELEMETRY = "127.0.0.1"
PORT_TELEMETRY = 9090
HOST_SNAPSHOT = "127.0.0.1"
PORT_SNAPSHOT = 9092
HOST_CONTROL = "127.0.0.1"
PORT_CONTROL = 9093


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


def snapshot_envelope(stamp_ns: int, data: dict) -> str:
    return json.dumps(
        {
            "schema_version": 1,
            "type": "carla_snapshot",
            "stamp_ns": stamp_ns,
            "data": data,
        }
    )


def bgr_to_jpeg_b64(carla_image: carla.Image) -> str:
    from PIL import Image

    img = Image.frombytes("RGBA", (carla_image.width, carla_image.height), bytes(carla_image.raw_data))
    buf = io.BytesIO()
    img.convert("RGB").save(buf, format="JPEG", quality=88)
    return base64.b64encode(buf.getvalue()).decode("ascii")


def classify_actor(actor: carla.Actor) -> str:
    tid = actor.type_id.lower()
    if "walker" in tid or "pedestrian" in tid:
        return "pedestrian"
    if "bike" in tid or "bicycle" in tid:
        return "cyclist"
    if "vehicle" in tid:
        return "vehicle"
    return "object"


def actor_to_object(actor: carla.Actor, ego_tf: carla.Transform) -> dict[str, Any]:
    tf = actor.get_transform()
    vel = actor.get_velocity()
    bb = actor.bounding_box
    return {
        "object_id": str(actor.id),
        "classification": classify_actor(actor),
        "existence_probability": 0.92,
        "position": {"x": tf.location.x, "y": tf.location.y, "z": tf.location.z},
        "yaw_rad": math.radians(tf.rotation.yaw),
        "dimensions_m": {"length": bb.extent.x * 2, "width": bb.extent.y * 2, "height": bb.extent.z * 2},
        "velocity_mps": {"x": vel.x, "y": vel.y},
    }


def lane_centerline_ahead(vehicle: carla.Vehicle, world: carla.World, ahead_m: float = 90.0, n: int = 48) -> list[dict]:
    wp = world.get_map().get_waypoint(
        vehicle.get_location(), project_to_road=True, lane_type=carla.LaneType.Driving
    )
    if wp is None:
        return []
    pts = []
    step = ahead_m / max(1, n - 1)
    for i in range(n):
        if i > 0:
            nxt = wp.next(step)
            if not nxt:
                break
            wp = nxt[0]
        loc = wp.transform.location
        rot = wp.transform.rotation
        pts.append(
            {
                "x": loc.x,
                "y": loc.y,
                "z": loc.z,
                "yaw_rad": math.radians(rot.yaw),
                "longitudinal_velocity_mps": math.hypot(vehicle.get_velocity().x, vehicle.get_velocity().y),
                "time_from_start_sec": i * step / 14.0,
            }
        )
    return pts


def traffic_light_state(actor: carla.Actor) -> str:
    try:
        state = actor.get_state()
        if state == carla.TrafficLightState.Red:
            return "red"
        if state == carla.TrafficLightState.Yellow:
            return "yellow"
        if state == carla.TrafficLightState.Green:
            return "green"
    except RuntimeError:
        pass
    return "unknown"


def scan_traffic_lights(world: carla.World, ego_loc: carla.Location, max_m: float = 70.0) -> list[dict]:
    out: list[dict] = []
    for actor in world.get_actors():
        if "traffic.traffic_light" not in actor.type_id:
            continue
        loc = actor.get_location()
        if ego_loc.distance(loc) > max_m:
            continue
        tf = actor.get_transform()
        out.append(
            {
                "id": str(actor.id),
                "state": traffic_light_state(actor),
                "position": {"x": loc.x, "y": loc.y, "z": loc.z},
                "yaw_rad": math.radians(tf.rotation.yaw),
            }
        )
    return out


def read_speed_limit_kmh(vehicle: carla.Vehicle, world: carla.World) -> float:
    wp = world.get_map().get_waypoint(vehicle.get_location(), project_to_road=True)
    if wp is None:
        return 50.0
    try:
        limit = float(wp.lane_limit)
        return limit * 3.6 if limit < 25 else limit
    except (AttributeError, TypeError):
        return 50.0


def scan_dynamic_actors(world: carla.World, ego: carla.Vehicle, ego_tf: carla.Transform, max_m: float = 55.0) -> list[dict]:
    actors: list[dict] = []
    ego_loc = ego.get_location()
    for actor in world.get_actors():
        if actor.id == ego.id:
            continue
        tid = actor.type_id.lower()
        if not (
            tid.startswith("vehicle.")
            or tid.startswith("walker.")
            or "bike" in tid
            or "bicycle" in tid
        ):
            continue
        aloc = actor.get_location()
        if ego_loc.distance(aloc) > max_m:
            continue
        actors.append(actor_to_object(actor, ego_tf))
    return actors


class CarlaAutonomyManager:
    """BasicAgent / BehaviorAgent wrapper for Phase C."""

    def __init__(self, vehicle: carla.Vehicle, world: carla.World, map_obj: carla.Map) -> None:
        self.vehicle = vehicle
        self.world = world
        self.map = map_obj
        self.mode = "off"
        self.state = "idle"
        self.maneuver = "standby"
        self._agent: Any = None
        self._grp: Any = None
        if GlobalRoutePlanner is not None:
            self._grp = GlobalRoutePlanner(map_obj, 2.0)

    def _destroy_agent(self) -> None:
        if self._agent is not None:
            try:
                self._agent.destroy()
            except RuntimeError:
                pass
            self._agent = None

    def _default_destination_ahead(self, ahead_m: float = 80.0) -> tuple[float, float]:
        tf = self.vehicle.get_transform()
        yaw = math.radians(tf.rotation.yaw)
        loc = self.vehicle.get_location()
        return (loc.x + ahead_m * math.cos(yaw), loc.y + ahead_m * math.sin(yaw))

    def start(self, mode: str, destination: tuple[float, float] | None) -> None:
        self._destroy_agent()
        self.vehicle.set_autopilot(False)
        self.mode = mode
        self.state = "driving"
        self.maneuver = "lane_follow"
        dest_xy = destination if destination is not None else self._default_destination_ahead()
        if self._grp is None or (BasicAgent is None and BehaviorAgent is None):
            self.maneuver = "agents_unavailable"
            return
        dest = carla.Location(x=dest_xy[0], y=dest_xy[1], z=0.5)
        if mode == "basic" and BasicAgent is not None:
            self._agent = BasicAgent(self.vehicle, opt_dict={"target_speed": 30})
            self._agent.set_destination(self._grp.trace_route(self.vehicle.get_location(), dest)[0])
        elif BehaviorAgent is not None:
            self._agent = BehaviorAgent(self.vehicle, behavior="normal")
            self._agent.set_destination(self._grp.trace_route(self.vehicle.get_location(), dest)[0])
        else:
            self.maneuver = "agent_unavailable"

    def pause(self) -> None:
        if self._agent is not None:
            self.vehicle.set_autopilot(False)
            self.vehicle.apply_control(carla.VehicleControl(brake=0.6))
        self.state = "paused"
        self.maneuver = "paused"

    def resume(self) -> None:
        if self._agent is not None:
            self.state = "driving"
            self.maneuver = "lane_follow"
        else:
            self.start(self.mode, None)

    def stop(self) -> None:
        self._destroy_agent()
        self.vehicle.set_autopilot(False)
        self.mode = "off"
        self.state = "idle"
        self.maneuver = "standby"

    def tick(self) -> None:
        if self._agent is None or self.state != "driving":
            return
        self._agent.run_step()


class CarlaVehicleController:
    """Applies dashboard control commands to the CARLA ego vehicle."""

    def __init__(self, vehicle: carla.Vehicle) -> None:
        self.vehicle = vehicle
        self.autopilot = False
        self.emergency = False
        self.throttle = 0.0
        self.brake = 0.0
        self.steer = 0.0
        self.hand_brake = False
        self.reverse = False
        self.dest: tuple[float, float] | None = None
        self.nav_arrow = "↑"
        self.nav_remain_m = 0.0
        self._route_pts: list[dict] = []
        self.autonomy_mode = "off"
        self.autonomy_state = "idle"
        self.autonomy_maneuver = "standby"
        self._route_total_m = 1.0

    def apply_command(self, data: dict[str, Any]) -> None:
        if data.get("emergency_stop"):
            self.emergency = True
            self.autopilot = False
            self.vehicle.set_autopilot(False)
            self.throttle = 0.0
            self.brake = 1.0
            self.steer = 0.0
            self.hand_brake = True
            return

        self.emergency = False
        self.throttle = float(data.get("throttle", 0.0))
        self.brake = float(data.get("brake", 0.0))
        self.steer = float(data.get("steer", 0.0))
        self.hand_brake = bool(data.get("hand_brake", False))
        self.reverse = bool(data.get("reverse", False))

        want_auto = bool(data.get("autopilot", False))
        if want_auto != self.autopilot:
            self.autopilot = want_auto
            self.vehicle.set_autopilot(want_auto)

        dest = data.get("destination")
        if isinstance(dest, dict) and "x" in dest and "y" in dest:
            self.set_destination(float(dest["x"]), float(dest["y"]))

        agent = data.get("autonomous_agent")
        action = data.get("autonomous_action")
        if isinstance(agent, str) and agent not in ("", "off"):
            self.autonomy_mode = agent
        if action == "start":
            self.autonomy_state = "driving"
        elif action == "pause":
            self.autonomy_state = "paused"
        elif action == "resume":
            self.autonomy_state = "driving"

    def set_destination(self, x: float, y: float) -> None:
        self.dest = (x, y)
        world = self.vehicle.get_world()
        m = world.get_map()
        start = m.get_waypoint(self.vehicle.get_location(), project_to_road=True)
        end = m.get_waypoint(carla.Location(x=x, y=y, z=0.5), project_to_road=True)
        pts: list[dict] = []
        if start and end and GlobalRoutePlanner is not None:
            try:
                grp = GlobalRoutePlanner(m, 2.0)
                route = grp.trace_route(start.transform.location, end.transform.location)
                for loc, _ in route:
                    pts.append(
                        {
                            "x": loc.x,
                            "y": loc.y,
                            "z": loc.z,
                            "yaw_rad": 0.0,
                            "longitudinal_velocity_mps": 8.0,
                            "time_from_start_sec": 0.0,
                        }
                    )
            except Exception:
                pts = []
        if not pts and start and end:
            cur = start
            for _ in range(64):
                loc = cur.transform.location
                pts.append(
                    {
                        "x": loc.x,
                        "y": loc.y,
                        "z": loc.z,
                        "yaw_rad": math.radians(cur.transform.rotation.yaw),
                        "longitudinal_velocity_mps": 8.0,
                        "time_from_start_sec": 0.0,
                    }
                )
                if cur.transform.location.distance(end.transform.location) < 3.0:
                    break
                nxt = cur.next(4.0)
                if not nxt:
                    break
                cur = nxt[0]
        self._route_pts = pts
        if pts:
            total = 0.0
            for i in range(1, len(pts)):
                total += math.hypot(pts[i]["x"] - pts[i - 1]["x"], pts[i]["y"] - pts[i - 1]["y"])
            self._route_total_m = max(1.0, total)
        dx = x - self.vehicle.get_location().x
        dy = y - self.vehicle.get_location().y
        self.nav_remain_m = math.hypot(dx, dy)
        yaw = math.radians(self.vehicle.get_transform().rotation.yaw)
        bearing = math.atan2(dy, dx)
        delta = bearing - yaw
        while delta > math.pi:
            delta -= 2 * math.pi
        while delta < -math.pi:
            delta += 2 * math.pi
        self.nav_arrow = "→" if delta > 0.45 else "←" if delta < -0.45 else "↑"

    def tick(self) -> None:
        if self.autopilot or self.emergency:
            if self.emergency:
                ctrl = carla.VehicleControl(throttle=0.0, brake=1.0, steer=0.0, hand_brake=True)
                self.vehicle.apply_control(ctrl)
            return
        ctrl = carla.VehicleControl(
            throttle=max(0.0, min(1.0, self.throttle)),
            brake=max(0.0, min(1.0, self.brake)),
            steer=max(-1.0, min(1.0, self.steer)),
            hand_brake=self.hand_brake,
            reverse=self.reverse,
        )
        if self.reverse:
            ctrl.manual_gear_shift = True
            ctrl.gear = -1
        else:
            ctrl.gear = 1
        self.vehicle.apply_control(ctrl)
        if self.dest:
            dx = self.dest[0] - self.vehicle.get_location().x
            dy = self.dest[1] - self.vehicle.get_location().y
            self.nav_remain_m = math.hypot(dx, dy)

    def route_points(self) -> list[dict]:
        return self._route_pts if self._route_pts else lane_centerline_ahead(self.vehicle, self.vehicle.get_world())

    def route_progress(self) -> float:
        if self.dest is None:
            return 0.0
        return max(0.0, min(1.0, 1.0 - self.nav_remain_m / self._route_total_m))

    def eta_sec(self, speed_mps: float) -> float:
        if speed_mps < 0.5:
            return 0.0
        return self.nav_remain_m / speed_mps

    def status_payload(self) -> dict[str, Any]:
        return {
            "type": "carla_status",
            "nav_arrow": self.nav_arrow,
            "nav_remain_m": round(self.nav_remain_m, 1),
            "autopilot": self.autopilot,
            "emergency": self.emergency,
            "navigation": {
                "arrow": self.nav_arrow,
                "remain_m": round(self.nav_remain_m, 1),
                "progress": round(self.route_progress(), 3),
            },
            "autonomy": {
                "mode": self.autonomy_mode,
                "state": self.autonomy_state,
                "maneuver": self.autonomy_maneuver,
            },
        }


def select_or_spawn_hero(world: carla.World, vehicle_filter: str = "vehicle.*") -> carla.Vehicle:
    """Same hero selection as CARLA no_rendering_mode.py (role_name=hero)."""
    heroes = [
        actor
        for actor in world.get_actors()
        if "vehicle" in actor.type_id and actor.attributes.get("role_name") == "hero"
    ]
    if heroes:
        hero = random.choice(heroes)
        print(f"Using existing hero vehicle id={hero.id} ({hero.type_id})")
        return hero

    bp_lib = world.get_blueprint_library()
    candidates = bp_lib.filter(vehicle_filter)
    if not candidates:
        raise RuntimeError(f"No vehicle blueprint matches filter {vehicle_filter!r}")
    blueprint = random.choice(candidates)
    blueprint.set_attribute("role_name", "hero")
    if blueprint.has_attribute("color"):
        color = random.choice(blueprint.get_attribute("color").recommended_values)
        blueprint.set_attribute("color", color)

    spawn_points = world.get_map().get_spawn_points()
    hero: carla.Vehicle | None = None
    for _ in range(max(1, len(spawn_points))):
        spawn_point = random.choice(spawn_points) if spawn_points else carla.Transform()
        hero = world.try_spawn_actor(blueprint, spawn_point)
        if hero is not None:
            break
    if hero is None:
        raise RuntimeError("Failed to spawn hero vehicle (map crowded — reduce traffic or retry)")
    print(f"Spawned hero vehicle id={hero.id} ({hero.type_id})")
    return hero


def apply_world_rendering_mode(world: carla.World, no_rendering: bool) -> None:
    settings = world.get_settings()
    settings.no_rendering_mode = no_rendering
    world.apply_settings(settings)
    if no_rendering:
        print("CARLA world: no_rendering_mode=ON (UE4 can stay minimized)")
    else:
        print("CARLA world: no_rendering_mode=OFF (windowed UE4 rendering)")


class CarlaSession:
    def __init__(
        self,
        host: str,
        port: int,
        role: str,
        *,
        hero_mode: bool = False,
        no_rendering: bool = False,
        vehicle_filter: str = "vehicle.*",
    ) -> None:
        self.client = carla.Client(host, port)
        self.client.set_timeout(10.0)
        self.world = self.client.get_world()
        apply_world_rendering_mode(self.world, no_rendering)
        bp_lib = self.world.get_blueprint_library()
        if hero_mode:
            self.vehicle = select_or_spawn_hero(self.world, vehicle_filter)
        else:
            vehicle_bp = bp_lib.filter(role)[0]
            spawn = self.world.get_map().get_spawn_points()[0]
            self.vehicle = self.world.spawn_actor(vehicle_bp, spawn)
        self.controller = CarlaVehicleController(self.vehicle)
        self.autonomy = CarlaAutonomyManager(self.vehicle, self.world, self.world.get_map())

        cam_bp = bp_lib.find("sensor.camera.rgb")
        cam_bp.set_attribute("image_size_x", "1280")
        cam_bp.set_attribute("image_size_y", "720")
        cam_bp.set_attribute("fov", "90")

        mounts = {
            "front": carla.Transform(carla.Location(x=1.6, z=1.45)),
            "rear": carla.Transform(carla.Location(x=-1.6, z=1.45), carla.Rotation(yaw=180)),
            "left": carla.Transform(carla.Location(y=-0.5, z=1.45), carla.Rotation(yaw=-90)),
            "right": carla.Transform(carla.Location(y=0.5, z=1.45), carla.Rotation(yaw=90)),
        }
        self.cameras: dict[str, carla.Actor] = {}
        self.latest_images: dict[str, dict[str, Any]] = {}
        self.active_camera = "front"

        for name, tr in mounts.items():
            cam = self.world.spawn_actor(cam_bp, tr, attach_to=self.vehicle)
            self.cameras[name] = cam
            cam.listen(lambda img, slot=name: self._on_image(slot, img))

        gnss_bp = bp_lib.find("sensor.other.gnss")
        self.gnss = self.world.spawn_actor(gnss_bp, carla.Transform(), attach_to=self.vehicle)
        imu_bp = bp_lib.find("sensor.other.imu")
        self.imu = self.world.spawn_actor(imu_bp, carla.Transform(), attach_to=self.vehicle)

        self.latest_gnss: dict[str, float] = {}
        self.latest_imu: dict[str, float] = {}
        self.gnss.listen(self._on_gnss)
        self.imu.listen(self._on_imu)

        map_name = self.world.get_map().name
        print(f"Connected to CARLA — map {map_name}")
        mode = "hero (no_rendering_mode compatible)" if hero_mode else "dedicated ego"
        print(f"SDV Digital Twin — {mode} · RGB 1st-person → dashboard :9090")
        self.vehicle.set_autopilot(False)

    def _on_image(self, slot: str, image: carla.Image) -> None:
        image.convert(carla.ColorConverterType.Raw)
        self.latest_images[slot] = {
            "payload_b64": bgr_to_jpeg_b64(image),
            "width": image.width,
            "height": image.height,
        }

    def _on_gnss(self, event: carla.GnssMeasurement) -> None:
        self.latest_gnss = {
            "latitude_deg": event.latitude,
            "longitude_deg": event.longitude,
            "altitude_m": event.altitude,
        }

    def _on_imu(self, event: carla.IMUMeasurement) -> None:
        self.latest_imu = {
            "accel_x_mps2": event.accelerometer.x,
            "accel_y_mps2": event.accelerometer.y,
            "accel_z_mps2": event.accelerometer.z,
            "gyro_x_rps": event.gyroscope.x,
            "gyro_y_rps": event.gyroscope.y,
            "gyro_z_rps": event.gyroscope.z,
        }

    def set_active_camera(self, name: str) -> None:
        if name in self.cameras:
            self.active_camera = name

    def collect(self) -> tuple[int, dict, list[dict], list[dict], dict]:
        self.autonomy.tick()
        self.controller.tick()
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
            "heading_rate_rps": self.latest_imu.get("gyro_z_rps", 0.0),
            "steering_angle_deg": control.steer * 70.0,
            "throttle": control.throttle,
            "brake": control.brake,
            "gear": 2 if control.reverse else (3 if speed > 0.5 else 1),
            "adas_active": self.controller.autopilot,
            "nav_arrow": self.controller.nav_arrow,
            "nav_remain_m": self.controller.nav_remain_m,
        }

        ego_loc = self.vehicle.get_location()
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

    def active_image(self) -> dict[str, Any]:
        return self.latest_images.get(self.active_camera, {})

    def destroy(self) -> None:
        for cam in self.cameras.values():
            cam.stop()
            cam.destroy()
        for sensor in (self.gnss, self.imu):
            if sensor:
                sensor.stop()
                sensor.destroy()
        if self.vehicle:
            self.vehicle.destroy()


async def telemetry_client_handler(websocket, session: CarlaSession) -> None:
    await websocket.send(envelope("bridge_hello", time.time_ns(), "", {"service": "carla-control-center-v2"}))
    while True:
        stamp, vehicle, actors, traj_pts, twin = session.collect()
        pos = vehicle["position"]
        for tl in twin.get("traffic_lights", []):
            actors.append(
                {
                    "object_id": tl["id"],
                    "classification": "traffic_light",
                    "tl_state": tl.get("state", "unknown"),
                    "existence_probability": 0.99,
                    "position": tl["position"],
                    "yaw_rad": tl.get("yaw_rad", 0.0),
                    "dimensions_m": {"length": 0.8, "width": 0.8, "height": 2.5},
                    "velocity_mps": {"x": 0.0, "y": 0.0},
                }
            )
        await websocket.send(
            envelope(
                "vehicle_state",
                stamp,
                "base_link",
                {
                    "stamp_ns": stamp,
                    "frame_id": "base_link",
                    "longitudinal_velocity_mps": vehicle["longitudinal_velocity_mps"],
                    "lateral_velocity_mps": vehicle["lateral_velocity_mps"],
                    "heading_rate_rps": vehicle["heading_rate_rps"],
                    "speed_mps": vehicle["speed_mps"],
                    "steering_angle_deg": vehicle["steering_angle_deg"],
                    "throttle": vehicle["throttle"],
                    "brake": vehicle["brake"],
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
                    "position": pos,
                    "orientation": {"w": 1.0, "x": 0.0, "y": 0.0, "z": 0.0},
                    "yaw_rad": vehicle["yaw_rad"],
                    "speed_mps": vehicle["speed_mps"],
                },
            )
        )
        await websocket.send(
            envelope("trajectory", stamp, "map", {"stamp_ns": stamp, "frame_id": "map", "points": traj_pts})
        )
        await websocket.send(
            envelope(
                "perception_objects",
                stamp,
                "map",
                {"stamp_ns": stamp, "frame_id": "map", "objects": actors},
            )
        )
        img = session.active_image()
        if img.get("payload_b64"):
            await websocket.send(
                envelope(
                    "front_camera",
                    stamp,
                    f"carla_rgb_{session.active_camera}",
                    {
                        "encoding": "base64",
                        "format": "jpeg",
                        "width": img.get("width", 1280),
                        "height": img.get("height", 720),
                        "payload": img["payload_b64"],
                        "slot": session.active_camera,
                    },
                )
            )
        await asyncio.sleep(0.1)


async def snapshot_client_handler(websocket, session: CarlaSession) -> None:
    while True:
        stamp, vehicle, actors, traj_pts, twin = session.collect()
        snap = {
            "vehicle": vehicle,
            "gnss": session.latest_gnss,
            "imu": {"accel_x_mps2": session.latest_imu.get("accel_x_mps2", 0.0)},
            "actors": actors,
            "trajectory_points": traj_pts,
            "camera_jpeg_b64": session.active_image().get("payload_b64", ""),
            "active_camera": session.active_camera,
            **twin,
        }
        await websocket.send(snapshot_envelope(stamp, snap))
        await asyncio.sleep(0.1)


async def control_client_handler(websocket, session: CarlaSession) -> None:
    await websocket.send(json.dumps({"type": "carla_status", "connected": True, "map": session.world.get_map().name}))
    async for raw in websocket:
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if msg.get("type") != "carla_control":
            continue
        data = msg.get("data") or {}
        session.controller.apply_command(data)
        agent = data.get("autonomous_agent")
        action = data.get("autonomous_action")
        if action == "start" and isinstance(agent, str) and agent not in ("", "off"):
            dest = session.controller.dest
            session.autonomy.start(agent, dest)
            session.controller.autonomy_mode = agent
            session.controller.autonomy_state = "driving"
        elif action == "pause":
            session.autonomy.pause()
            session.controller.autonomy_state = "paused"
        elif action == "resume":
            session.autonomy.resume()
            session.controller.autonomy_state = "driving"
        elif data.get("emergency_stop"):
            session.autonomy.stop()
        cam = data.get("camera")
        if isinstance(cam, str):
            session.set_active_camera(cam)
        status = session.controller.status_payload()
        status["autonomy"] = {
            "mode": session.autonomy.mode,
            "state": session.autonomy.state,
            "maneuver": session.autonomy.maneuver,
        }
        await websocket.send(json.dumps(status))


async def run_servers(
    host: str,
    port: int,
    role: str,
    snapshot_port: int,
    control_port: int,
    *,
    hero_mode: bool = False,
    no_rendering: bool = False,
    vehicle_filter: str = "vehicle.*",
) -> None:
    import websockets

    session = CarlaSession(
        host,
        port,
        role,
        hero_mode=hero_mode,
        no_rendering=no_rendering,
        vehicle_filter=vehicle_filter,
    )

    async def handler_telemetry(ws):
        await telemetry_client_handler(ws, session)

    async def handler_snapshot(ws):
        await snapshot_client_handler(ws, session)

    async def handler_control(ws):
        await control_client_handler(ws, session)

    async with websockets.serve(handler_telemetry, HOST_TELEMETRY, PORT_TELEMETRY):
        async with websockets.serve(handler_snapshot, HOST_SNAPSHOT, snapshot_port):
            async with websockets.serve(handler_control, HOST_CONTROL, control_port):
                print("SDV Digital Twin — CARLA bridge")
                print(f"  Telemetry: ws://{HOST_TELEMETRY}:{PORT_TELEMETRY}")
                print(f"  Snapshots: ws://{HOST_SNAPSHOT}:{snapshot_port}")
                print(f"  Control:   ws://{HOST_CONTROL}:{control_port}")
                print("  Dashboard: SDV_Dashboard.exe --carla-mode bridge")
                try:
                    await asyncio.Future()
                finally:
                    session.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description="CARLA 0.9.15 bridge for SDV Dashboard")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2000)
    parser.add_argument("--role", default="vehicle.tesla.model3", help="Blueprint when --hero is off")
    parser.add_argument("--snapshot-port", type=int, default=PORT_SNAPSHOT)
    parser.add_argument("--control-port", type=int, default=PORT_CONTROL)
    parser.add_argument(
        "--hero",
        action="store_true",
        help="Attach to role_name=hero vehicle (use with pygame no_rendering_mode; do not spawn second ego)",
    )
    parser.add_argument(
        "--no-rendering",
        action="store_true",
        help="Enable CARLA world no_rendering_mode (optional; UE4 may stay windowed)",
    )
    parser.add_argument(
        "--filter",
        default="vehicle.*",
        dest="vehicle_filter",
        metavar="PATTERN",
        help="Hero spawn blueprint filter when --hero (default: vehicle.*)",
    )
    args = parser.parse_args()
    asyncio.run(
        run_servers(
            args.host,
            args.port,
            args.role,
            args.snapshot_port,
            args.control_port,
            hero_mode=args.hero,
            no_rendering=args.no_rendering,
            vehicle_filter=args.vehicle_filter,
        )
    )


if __name__ == "__main__":
    main()
