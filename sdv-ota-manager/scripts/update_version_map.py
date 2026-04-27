#!/usr/bin/env python3
"""Append or update a package entry in deploy/version_map.json from manifest.json."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load_version_map(path: Path) -> dict:
    if not path.exists() or path.stat().st_size == 0:
        return {"schema_version": 1, "packages": []}
    with path.open(encoding="utf-8-sig") as f:
        data = json.load(f)
    if isinstance(data, list):
        return {"schema_version": 1, "packages": data}
    if isinstance(data, dict) and "packages" in data:
        return data
    raise ValueError(f"Unsupported version_map format in {path}")


def entry_key(entry: dict) -> tuple:
    return (
        str(entry.get("domain", "")),
        str(entry.get("feature", "")),
        str(entry.get("version", "")),
    )


def manifest_to_entry(manifest: dict, package_dir: Path, ota_root: Path) -> dict:
    rel = package_dir.relative_to(ota_root)
    entry = dict(manifest)
    entry["package_path"] = str(rel).replace("\\", "/")
    return entry


def main() -> int:
    parser = argparse.ArgumentParser(description="Update deploy/version_map.json from a package manifest.")
    parser.add_argument(
        "--package-dir",
        required=True,
        type=Path,
        help="Directory containing manifest.json (e.g. deploy/packages/ADAS/v0.3)",
    )
    args = parser.parse_args()

    package_dir = args.package_dir.resolve()
    manifest_path = package_dir / "manifest.json"
    if not manifest_path.is_file():
        print(f"manifest.json not found: {manifest_path}", file=sys.stderr)
        return 1

    with manifest_path.open(encoding="utf-8-sig") as f:
        manifest = json.load(f)

    script_dir = Path(__file__).resolve().parent
    ota_root = script_dir.parent
    version_map_path = ota_root / "deploy" / "version_map.json"

    entry = manifest_to_entry(manifest, package_dir, ota_root)
    key = entry_key(entry)

    data = load_version_map(version_map_path)
    packages: list = data.setdefault("packages", [])
    replaced = False
    for i, existing in enumerate(packages):
        if entry_key(existing) == key:
            packages[i] = entry
            replaced = True
            break
    if not replaced:
        packages.append(entry)

    version_map_path.parent.mkdir(parents=True, exist_ok=True)
    with version_map_path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")

    action = "updated" if replaced else "appended"
    print(f"version_map.json {action}: {key}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
