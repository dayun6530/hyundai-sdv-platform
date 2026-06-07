#!/usr/bin/env python3
"""
Legacy name: launches pygame_map_bridge (Dashboard cockpit feed).

For a separate pygame window only (no Dashboard), use CARLA's example:
  CARLA_ROOT\\PythonAPI\\examples\\no_rendering_mode.py
"""
from __future__ import annotations

import runpy
import sys
from pathlib import Path

_TARGET = Path(__file__).resolve().parent / "pygame_map_bridge.py"


def main() -> None:
    print("SDV: pygame map → Dashboard (use pygame_map_bridge.py)")
    sys.argv = [str(_TARGET)] + sys.argv[1:]
    runpy.run_path(str(_TARGET), run_name="__main__")


if __name__ == "__main__":
    main()
