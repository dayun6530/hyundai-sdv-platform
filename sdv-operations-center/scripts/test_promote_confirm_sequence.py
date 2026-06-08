#!/usr/bin/env python3
"""Smoke-test per-car promote_primary_prompt on pygame bridge (:9093)."""
from __future__ import annotations

import asyncio
import json
import sys

try:
    import websockets
except ImportError:
    print("FAIL: pip install websockets")
    sys.exit(1)

WS = "ws://127.0.0.1:9093"
CARS = ["car_1", "car_2", "car_3"]


async def one_car_prompt(car_id: str, index: int, total: int) -> bool:
    request_id = f"test-{car_id}-{index}"
    async with websockets.connect(WS, open_timeout=3) as ws:
        raw = await ws.recv()
        status = json.loads(raw)
        if status.get("type") != "carla_status":
            print(f"  unexpected hello: {status}")

        await ws.send(
            json.dumps(
                {
                    "type": "promote_primary_prompt",
                    "request_id": request_id,
                    "car_id": car_id,
                    "index": index,
                    "total": total,
                    "feature": "ADAS",
                    "version": "v2.0",
                    "message": f"{car_id} — ADAS Primary 업데이트?",
                    "detail": f"차량: {car_id}\n({index}/{total})",
                }
            )
        )
        ack = json.loads(await ws.recv())
        if ack.get("type") != "promote_primary_prompt_ack":
            print(f"  FAIL {car_id}: no ack ({ack})")
            return False

        await ws.send(
            json.dumps(
                {
                    "type": "promote_primary_answer",
                    "request_id": request_id,
                    "accepted": index % 2 == 1,
                }
            )
        )
        resp = json.loads(await ws.recv())
        ok = (
            resp.get("type") == "promote_primary_response"
            and resp.get("request_id") == request_id
            and resp.get("accepted") is (index % 2 == 1)
        )
        print(
            f"  {car_id} ({index}/{total}): "
            f"{'OK' if ok else 'FAIL'} accepted={resp.get('accepted')}"
        )
        return ok


async def main() -> int:
    print(f"Connecting {WS} ...")
    total = len(CARS)
    results = []
    for i, car_id in enumerate(CARS, start=1):
        try:
            results.append(await one_car_prompt(car_id, i, total))
        except Exception as exc:
            print(f"  {car_id}: ERROR {exc}")
            results.append(False)

    passed = sum(1 for r in results if r)
    print(f"\nResult: {passed}/{len(CARS)} per-car prompts OK")
    if passed == len(CARS):
        print("PASS — pygame bridge handles sequential per-car promote confirm.")
        return 0
    print("FAIL - is pygame_map_bridge.py running?")
    return 1


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
