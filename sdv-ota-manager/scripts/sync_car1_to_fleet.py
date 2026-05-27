#!/usr/bin/env python3
"""Copy car_1 deploy state (ADAS v4.0 primary) to car_2..car_10."""
import copy
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OTA = ROOT / "sdv-ota-manager"
FFP = ROOT / "sdv-feature-flag-platform"
STATE_DIR = OTA / "platform/deploy/state"
RT_DIR = OTA / "deploy/vehicle_runtime"
CFG_OTA = OTA / "platform/config/car"
CFG_FFP = FFP / "config/car"
CAMPAIGNS = OTA / "platform/deploy/campaigns"
VM = OTA / "platform/deploy/version_map.json"


def main() -> None:
    car1_state = json.loads((STATE_DIR / "car_1.json").read_text(encoding="utf-8"))
    car1_rt = json.loads((RT_DIR / "car_1.json").read_text(encoding="utf-8"))
    car1_cfg = (CFG_OTA / "car_1.conf").read_text(encoding="utf-8")
    car1_campaign = json.loads(
        (CAMPAIGNS / "campaign_adas_v4.0_car1_canary.json").read_text(encoding="utf-8")
    )

    fleet_block = ""
    if "# --- fleet deploy" in car1_cfg:
        fleet_block = car1_cfg[car1_cfg.index("# --- fleet deploy") :]

    vm = json.loads(VM.read_text(encoding="utf-8"))
    vm["features"]["ADAS"]["B"] = {
        "version": "v4.0",
        "role": "candidate",
        "binary": "AdasFeature",
        "package_id": "ADAS_v4.0",
        "package_path": "deploy/packages/ADAS/v4.0",
    }
    VM.write_text(json.dumps(vm, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    def sub_car(s: str, n: int) -> str:
        return s.replace("car_1", f"car_{n}")

    for n in range(2, 11):
        car = f"car_{n}"
        existing_path = STATE_DIR / f"{car}.json"
        existing = json.loads(existing_path.read_text(encoding="utf-8"))

        state = copy.deepcopy(car1_state)
        for key in ("car_id", "model", "region", "group", "status", "last_seen_at", "heartbeat_source"):
            if key in existing:
                state[key] = existing[key]
        state["car_id"] = car

        adas = copy.deepcopy(car1_state["features"]["ADAS"])
        adas["docker_runtime_container"] = f"{car}_agent_adas"
        state["features"]["ADAS"] = adas
        state["features"]["Navigation"] = copy.deepcopy(car1_state["features"]["Navigation"])
        state["features"]["Energy"] = copy.deepcopy(car1_state["features"]["Energy"])
        state["ota"] = copy.deepcopy(car1_state["ota"])

        if "last_deploy_id" in car1_state:
            state["last_deploy_id"] = sub_car(car1_state["last_deploy_id"], n)
        for k in (
            "last_deploy_result",
            "last_deploy_time",
            "last_deploy_feature",
            "last_deploy_slot",
            "last_deploy_version",
            "last_deploy_reason",
        ):
            if k in car1_state:
                state[k] = car1_state[k]
        if "last_ack" in car1_state:
            state["last_ack"] = copy.deepcopy(car1_state["last_ack"])

        existing_path.write_text(json.dumps(state, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

        rt = copy.deepcopy(car1_rt)
        rt["car_id"] = car
        domain_containers = {
            "adas": "adas",
            "navigation": "navigation",
            "evenergy": "evenergy",
        }
        for domain, suffix in domain_containers.items():
            d = rt["domains"][domain]
            d["primary_container"] = f"{car}_agent_{suffix}"
            d["shadow_container"] = f"{car}_agent_{suffix}_shadow"
        (RT_DIR / f"{car}.json").write_text(
            json.dumps(rt, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )

        base_cfg_path = CFG_FFP / f"{car}.conf"
        base = base_cfg_path.read_text(encoding="utf-8")
        if "# --- fleet deploy" in base:
            base = base[: base.index("# --- fleet deploy")].rstrip() + "\n"
        port = 50050 + n
        base = re.sub(r"address 0\.0\.0\.0:\d+", f"address 0.0.0.0:{port}", base, count=1)
        base = re.sub(
            r"# SDV Feature Flag Platform Configuration \(car_\d+\)",
            f"# SDV Feature Flag Platform Configuration ({car})",
            base,
        )
        base = re.sub(
            r"Experiment group instance: car_\d+",
            f"Experiment group instance: {car}",
            base,
        )
        ota_cfg = base.rstrip() + "\n\n\n" + fleet_block
        CFG_OTA.mkdir(parents=True, exist_ok=True)
        (CFG_OTA / f"{car}.conf").write_text(ota_cfg, encoding="utf-8")

        ffp_cfg = base.rstrip() + "\n\n\n" + fleet_block
        base_cfg_path.write_text(ffp_cfg, encoding="utf-8")

        camp = copy.deepcopy(car1_campaign)
        camp["campaign_id"] = f"campaign_adas_v4.0_{car}_canary"
        camp["name"] = f"ADAS v4.0 candidate rollout to {car}"
        camp["description"] = (
            f"Deploy ADAS v4.0 to {car} as candidate, verify logs, then promote to primary."
        )
        camp["target_selector"] = {"scope": "vehicle", "vehicle_ids": [car]}
        (CAMPAIGNS / f"campaign_adas_v4.0_{car}_canary.json").write_text(
            json.dumps(camp, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )

    print("OK: synced car_2..car_10 from car_1; version_map B=v4.0")


if __name__ == "__main__":
    main()
