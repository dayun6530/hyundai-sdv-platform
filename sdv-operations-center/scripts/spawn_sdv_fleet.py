#!/usr/bin/env python3
"""
Spawn SDV fleet car_1..car_10 in CARLA (mapped to WSL car_N processes).

  car_1  -> role_name=hero      (pygame bridge attaches; user drives)
  car_2..car_10 -> role_name=sdv_car_N + Traffic Manager autopilot

Usage:
  python spawn_sdv_fleet.py
  python spawn_sdv_fleet.py --count 10 --host 127.0.0.1 --port 2000
"""
from __future__ import annotations

import argparse
import logging
import random
import sys
import time
from typing import Dict, Optional

try:
    import carla
except ImportError as exc:
    raise SystemExit("Install CARLA 0.9.15 Python API") from exc


def car_id_from_role(role: str) -> Optional[str]:
    if role == "hero":
        return "car_1"
    if role.startswith("sdv_car_"):
        return role.replace("sdv_", "", 1)
    return None


def role_name_for_car_id(car_id: str) -> str:
    if car_id == "car_1":
        return "hero"
    return f"sdv_{car_id}"


def existing_fleet(world: carla.World) -> Dict[str, carla.Vehicle]:
    found: dict[str, carla.Vehicle] = {}
    for actor in world.get_actors():
        if "vehicle" not in actor.type_id:
            continue
        role = actor.attributes.get("role_name", "")
        car_id = car_id_from_role(role)
        if car_id:
            found[car_id] = actor
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=2000)
    parser.add_argument("--count", type=int, default=10, help="SDV vehicles car_1..car_N (max 10)")
    parser.add_argument("--tm-port", type=int, default=8000)
    parser.add_argument("--seed", type=int, default=None)
    args = parser.parse_args()

    logging.basicConfig(format="%(levelname)s: %(message)s", level=logging.INFO)
    count = max(1, min(10, int(args.count)))
    fleet_ids = [f"car_{i}" for i in range(1, count + 1)]

    if args.seed is not None:
        random.seed(args.seed)

    client = carla.Client(args.host, args.port)
    client.set_timeout(15.0)
    world = client.get_world()
    tm = client.get_trafficmanager(args.tm_port)
    tm.set_global_distance_to_leading_vehicle(2.5)

    bp_lib = world.get_blueprint_library()
    blueprints = sorted(bp_lib.filter("vehicle.*"), key=lambda bp: bp.id)
    if not blueprints:
        raise SystemExit("No vehicle blueprints found")

    spawn_points = world.get_map().get_spawn_points()
    if not spawn_points:
        raise SystemExit("No spawn points on current map")

    random.shuffle(spawn_points)
    already = existing_fleet(world)
    if already:
        logging.info("Existing SDV fleet in CARLA: %s", ", ".join(sorted(already.keys())))

    SpawnActor = carla.command.SpawnActor
    SetAutopilot = carla.command.SetAutopilot
    FutureActor = carla.command.FutureActor

    batch = []
    spawn_idx = 0
    spawned_ids: list[int] = []

    for car_id in fleet_ids:
        if car_id in already:
            logging.info("Skip %s — already present (id=%s)", car_id, already[car_id].id)
            continue
        if spawn_idx >= len(spawn_points):
            logging.warning("No free spawn points left for %s", car_id)
            break

        blueprint = random.choice(blueprints)
        if blueprint.has_attribute("color"):
            color = random.choice(blueprint.get_attribute("color").recommended_values)
            blueprint.set_attribute("color", color)
        blueprint.set_attribute("role_name", role_name_for_car_id(car_id))

        transform = spawn_points[spawn_idx]
        spawn_idx += 1
        cmd = SpawnActor(blueprint, transform)
        if car_id != "car_1":
            cmd = cmd.then(SetAutopilot(FutureActor, True, tm.get_port()))
        batch.append((car_id, cmd))

    for car_id, cmd in batch:
        responses = client.apply_batch_sync([cmd], False)
        for response in responses:
            if response.error:
                logging.error("%s spawn failed: %s", car_id, response.error)
            else:
                spawned_ids.append(response.actor_id)
                logging.info("Spawned %s actor id=%s role=%s", car_id, response.actor_id, role_name_for_car_id(car_id))

    total = len(existing_fleet(world))
    print(
        f"SDV fleet ready: {total}/{count} vehicles in CARLA "
        f"(car_1=hero, car_2..car_{count}=autopilot). Keep this window open.",
        flush=True,
    )

    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        print("\nStopping (vehicles remain in CARLA until map reset).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
