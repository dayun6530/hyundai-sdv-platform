#!/usr/bin/env python3
"""Read-only package registry view for Feature/Version/Docker metadata.

This Phase 3-C prototype intentionally does not modify package_registry.json,
manifest.json, vehicles.json, C++ code, dashboard code, status-server code,
scripts, or CMake files.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


JsonObject = Dict[str, Any]


def _read_json_file(path: Path) -> Tuple[Optional[Any], Optional[str]]:
    try:
        if not path.exists():
            return None, f"missing file: {path.as_posix()}"
        with path.open("r", encoding="utf-8") as f:
            return json.load(f), None
    except json.JSONDecodeError as exc:
        return None, f"invalid JSON in {path.as_posix()}: {exc}"
    except OSError as exc:
        return None, f"failed to read {path.as_posix()}: {exc}"


def _normalize_feature(value: Any) -> str:
    raw = str(value or "").strip()
    compact = raw.lower().replace("_", "").replace("-", "").replace(" ", "")
    if compact in {"adas", "adasfeature"}:
        return "ADAS"
    if compact in {"navigation", "nav", "navigationfeature"}:
        return "Navigation"
    if compact in {"energy", "evenergy", "ev", "evenergyfeature"}:
        return "Energy"
    return raw


def _version_key(value: Any) -> str:
    s = str(value or "").strip()
    if s.lower().startswith("v"):
        return s[1:]
    return s


def _version_matches(left: Any, right: Any) -> bool:
    return _version_key(left) == _version_key(right)


def _string_or_none(value: Any) -> Optional[str]:
    if value is None:
        return None
    s = str(value).strip()
    return s or None


def _string_list(value: Any) -> List[str]:
    if isinstance(value, list):
        return [str(v).strip() for v in value if str(v).strip()]
    if isinstance(value, str):
        return [part.strip() for part in value.replace(";", ",").split(",") if part.strip()]
    return []


class PackageRegistryAdapter:
    """Read-only adapter over package_registry.json and package manifests."""

    def __init__(self, root: str | Path = ".") -> None:
        self.root = Path(root).resolve()
        self.platform_root = self.root / "sdv-ota-manager" / "platform"
        self.deploy_root = self.platform_root / "deploy"
        self.package_registry_path = self.deploy_root / "package_registry.json"
        self.packages_root = self.deploy_root / "packages"
        self.vehicles_path = self.platform_root / "config" / "fleet" / "vehicles.json"

        self._package_registry: Optional[JsonObject] = None
        self._manifest_cache: Optional[List[JsonObject]] = None
        self._vehicles: Optional[List[JsonObject]] = None
        self.warnings: List[str] = []

    def load_package_registry(self) -> JsonObject:
        data, warning = _read_json_file(self.package_registry_path)
        if warning:
            self._add_warning(warning)
            self._package_registry = {"packages": []}
            return {"packages": []}
        if not isinstance(data, dict):
            self._add_warning(f"package_registry is not an object: {self.package_registry_path.as_posix()}")
            self._package_registry = {"packages": []}
            return {"packages": []}
        if not isinstance(data.get("packages"), list):
            self._add_warning("package_registry.packages is missing or not a list")
            data = {**data, "packages": []}
        self._package_registry = data
        return data

    def load_package_manifest(self, feature: str, version: str) -> JsonObject:
        manifest = self.get_manifest(feature, version)
        return manifest or {}

    def list_features(self) -> List[str]:
        features = set()
        for package in self._all_package_records():
            feature = self._record_feature(package)
            if feature:
                features.add(feature)
        return sorted(features)

    def list_packages(self, feature: Optional[str] = None) -> List[JsonObject]:
        target = _normalize_feature(feature) if feature else None
        packages = []
        for record in self._all_package_records():
            view = self._to_package_view_from_record(record)
            if target and view.get("feature") != target:
                continue
            packages.append(view)
        return self._dedupe_package_views(packages)

    def find_package(self, feature: str, version: str) -> Optional[JsonObject]:
        target = _normalize_feature(feature)
        for package in self.list_packages(target):
            if _version_matches(package.get("version"), version):
                return package
        return None

    def get_manifest(self, feature: str, version: str) -> Optional[JsonObject]:
        target = _normalize_feature(feature)
        for manifest in self._load_manifests():
            if self._record_feature(manifest) == target and _version_matches(manifest.get("version") or manifest.get("_version_from_path"), version):
                return manifest
        return None

    def get_docker_image(self, feature: str, version: str) -> Optional[str]:
        package = self.find_package(feature, version)
        return _string_or_none(package.get("docker_image")) if package else None

    def get_image_digest(self, feature: str, version: str) -> Optional[str]:
        package = self.find_package(feature, version)
        return _string_or_none(package.get("image_digest")) if package else None

    def get_compatibility(self, feature: str, version: str) -> JsonObject:
        package = self.find_package(feature, version)
        if not package:
            return {"compatible_models": [], "compatible_regions": []}
        return {
            "compatible_models": package.get("compatible_models") or [],
            "compatible_regions": package.get("compatible_regions") or [],
        }

    def validate_package_for_vehicle(self, feature: str, version: str, vehicle_id: str) -> JsonObject:
        package = self.find_package(feature, version)
        vehicle = self._find_vehicle(vehicle_id)
        reasons: List[str] = []

        if not package:
            return {
                "vehicle_id": vehicle_id,
                "feature": _normalize_feature(feature),
                "version": version,
                "compatible": False,
                "reasons": ["Package metadata is not available"],
            }

        if not vehicle:
            return {
                "vehicle_id": vehicle_id,
                "feature": package.get("feature"),
                "version": package.get("version"),
                "compatible": None,
                "reasons": ["Vehicle metadata is not available"],
            }

        model = _string_or_none(vehicle.get("model"))
        region = _string_or_none(vehicle.get("region"))
        compatible_models = package.get("compatible_models") or []
        compatible_regions = package.get("compatible_regions") or []

        if not model or not region:
            return {
                "vehicle_id": vehicle_id,
                "feature": package.get("feature"),
                "version": package.get("version"),
                "compatible": None,
                "reasons": ["Vehicle model or region metadata is not available"],
            }

        if compatible_models and model not in compatible_models:
            reasons.append(f"Vehicle model {model} is not in compatible_models")
        if compatible_regions and region not in compatible_regions:
            reasons.append(f"Vehicle region {region} is not in compatible_regions")

        return {
            "vehicle_id": vehicle_id,
            "feature": package.get("feature"),
            "version": package.get("version"),
            "compatible": not reasons,
            "reasons": reasons,
            "vehicle": {
                "model": model,
                "region": region,
                "group": vehicle.get("group"),
                "status": vehicle.get("status"),
            },
        }

    def to_package_view(self, feature: str, version: str) -> JsonObject:
        package = self.find_package(feature, version)
        if not package:
            return {
                "feature": _normalize_feature(feature),
                "version": version,
                "docker_image": None,
                "image_digest": None,
                "status": "UNKNOWN",
                "compatible_models": [],
                "compatible_regions": [],
                "deployable": False,
                "reasons": ["Package metadata is not available"],
            }
        return package

    def summarize_packages(self) -> JsonObject:
        features: JsonObject = {}
        packages = self.list_packages()
        for package in packages:
            feature = package.get("feature") or "unknown"
            item = features.setdefault(feature, {
                "count": 0,
                "versions": [],
                "docker_images": 0,
                "with_digest": 0,
                "deployable": 0,
            })
            item["count"] += 1
            if package.get("version"):
                item["versions"].append(package["version"])
            if package.get("docker_image"):
                item["docker_images"] += 1
            if package.get("image_digest"):
                item["with_digest"] += 1
            if package.get("deployable") is True:
                item["deployable"] += 1

        for item in features.values():
            item["versions"] = sorted(set(item["versions"]), key=_version_key)

        return {
            "ok": not self.warnings,
            "root": self.root.as_posix(),
            "package_registry": self.package_registry_path.as_posix(),
            "packages_root": self.packages_root.as_posix(),
            "package_count": len(packages),
            "features": features,
            "warnings": self.warnings,
        }

    def _add_warning(self, message: str) -> None:
        if message not in self.warnings:
            self.warnings.append(message)

    def _get_package_registry(self) -> JsonObject:
        if self._package_registry is None:
            return self.load_package_registry()
        return self._package_registry

    def _all_package_records(self) -> List[JsonObject]:
        records: List[JsonObject] = []
        registry = self._get_package_registry()
        registry_packages = registry.get("packages")
        if isinstance(registry_packages, list):
            records.extend(p for p in registry_packages if isinstance(p, dict))
        records.extend(self._load_manifests())
        return records

    def _load_manifests(self) -> List[JsonObject]:
        if self._manifest_cache is not None:
            return self._manifest_cache
        if not self.packages_root.exists():
            self._add_warning(f"missing packages directory: {self.packages_root.as_posix()}")
            self._manifest_cache = []
            return []

        manifests: List[JsonObject] = []
        for path in self.packages_root.glob("*/*/manifest.json"):
            data, warning = _read_json_file(path)
            if warning:
                self._add_warning(warning)
                continue
            if not isinstance(data, dict):
                self._add_warning(f"manifest is not an object: {path.as_posix()}")
                continue
            manifests.append({
                **data,
                "_manifest_path_abs": path.as_posix(),
                "_feature_from_path": path.parent.parent.name,
                "_version_from_path": path.parent.name,
            })
        self._manifest_cache = manifests
        return manifests

    def _load_vehicles(self) -> List[JsonObject]:
        if self._vehicles is not None:
            return self._vehicles
        data, warning = _read_json_file(self.vehicles_path)
        if warning:
            self._add_warning(warning)
            self._vehicles = []
            return []
        if not isinstance(data, dict) or not isinstance(data.get("vehicles"), list):
            self._add_warning("vehicles.json is missing vehicles list")
            self._vehicles = []
            return []
        self._vehicles = [v for v in data["vehicles"] if isinstance(v, dict)]
        return self._vehicles

    def _find_vehicle(self, vehicle_id: str) -> Optional[JsonObject]:
        target = str(vehicle_id or "").strip()
        if not target:
            return None
        for vehicle in self._load_vehicles():
            if str(vehicle.get("car_id") or "").strip() == target:
                return vehicle
        return None

    def _record_feature(self, record: JsonObject) -> str:
        return _normalize_feature(record.get("feature") or record.get("domain") or record.get("_feature_from_path"))

    def _to_package_view_from_record(self, record: JsonObject) -> JsonObject:
        feature = self._record_feature(record)
        version = _string_or_none(record.get("version") or record.get("_version_from_path"))
        artifact_type = _string_or_none(record.get("artifact_type") or record.get("package_type")) or "unknown"
        docker_image = _string_or_none(record.get("docker_image"))
        image_digest = _string_or_none(record.get("image_digest"))
        checksum_file = _string_or_none(record.get("checksum_file"))
        manifest_abs = _string_or_none(record.get("_manifest_path_abs"))
        package_path = _string_or_none(record.get("package_path"))

        deployable, reasons = self._deployability(record, artifact_type, docker_image, image_digest, checksum_file, manifest_abs)

        return {
            "package_id": _string_or_none(record.get("package_id")),
            "feature": feature,
            "version": version,
            "role": _string_or_none(record.get("role") or record.get("default_role") or record.get("slot_role")),
            "artifact_type": artifact_type,
            "docker_image": docker_image,
            "image_digest": image_digest,
            "image_digest_source": _string_or_none(record.get("image_digest_source")),
            "checksum_file": checksum_file,
            "checksum": _string_or_none(record.get("checksum")),
            "binary": _string_or_none(record.get("binary") or record.get("binary_name") or record.get("executable_name")),
            "compatible_models": _string_list(record.get("compatible_models")),
            "compatible_regions": _string_list(record.get("compatible_regions")),
            "release_note": _string_or_none(record.get("release_note") or record.get("description")),
            "status": _string_or_none(record.get("status")) or ("READY" if deployable else "INCOMPLETE"),
            "package_path": package_path,
            "manifest_path": _string_or_none(record.get("manifest_path")),
            "manifest_path_abs": manifest_abs,
            "deployable": deployable,
            "deployability_reasons": reasons,
        }

    def _deployability(
        self,
        record: JsonObject,
        artifact_type: str,
        docker_image: Optional[str],
        image_digest: Optional[str],
        checksum_file: Optional[str],
        manifest_abs: Optional[str],
    ) -> Tuple[bool, List[str]]:
        reasons: List[str] = []
        if not _string_or_none(record.get("version") or record.get("_version_from_path")):
            reasons.append("version is missing")
        if not self._record_feature(record):
            reasons.append("feature is missing")

        if artifact_type == "docker_image" or docker_image:
            if not docker_image:
                reasons.append("docker_image is missing")
            if not image_digest and not checksum_file:
                reasons.append("both image_digest and checksum_file are missing")
            if checksum_file and manifest_abs:
                checksum_path = Path(manifest_abs).parent / checksum_file
                if not checksum_path.exists() and not image_digest:
                    reasons.append(f"checksum_file is missing: {checksum_path.as_posix()}")

        return (not reasons), reasons

    def _dedupe_package_views(self, packages: List[JsonObject]) -> List[JsonObject]:
        deduped: Dict[Tuple[str, str, str], JsonObject] = {}
        for package in packages:
            key = (
                str(package.get("feature") or ""),
                _version_key(package.get("version")),
                str(package.get("package_id") or package.get("manifest_path_abs") or ""),
            )
            existing = deduped.get(key)
            if not existing:
                deduped[key] = package
                continue
            merged = {**existing}
            for field, value in package.items():
                if merged.get(field) in (None, "", [], {}):
                    merged[field] = value
            # Merge deployability conservatively: any incomplete source should keep its reasons visible.
            reasons = []
            for source in (existing, package):
                if isinstance(source.get("deployability_reasons"), list):
                    reasons.extend(str(r) for r in source["deployability_reasons"] if str(r))
            merged["deployability_reasons"] = sorted(set(reasons))
            merged["deployable"] = not merged["deployability_reasons"]
            if merged.get("status") in (None, "", "INCOMPLETE") and merged["deployable"]:
                merged["status"] = "READY"
            deduped[key] = merged
        return sorted(deduped.values(), key=lambda p: (str(p.get("feature") or ""), _version_key(p.get("version"))))


def main() -> int:
    parser = argparse.ArgumentParser(description="Read-only package registry adapter.")
    parser.add_argument("--root", default=".", help="Repository root path (default: current directory)")
    parser.add_argument("--summary", action="store_true", help="Print package summary JSON")
    parser.add_argument("--feature", default="", help="Optional feature filter, e.g. ADAS")
    parser.add_argument("--version", default="", help="Optional version filter, e.g. v0.4")
    parser.add_argument("--vehicle", default="", help="Optional vehicle compatibility check, e.g. car_1")
    args = parser.parse_args()

    adapter = PackageRegistryAdapter(args.root)

    if args.summary:
        print(json.dumps(adapter.summarize_packages(), indent=2, ensure_ascii=False))
        return 0

    if args.feature and args.version and args.vehicle:
        print(json.dumps(
            adapter.validate_package_for_vehicle(args.feature, args.version, args.vehicle),
            indent=2,
            ensure_ascii=False,
        ))
        return 0

    if args.feature and args.version:
        print(json.dumps(adapter.to_package_view(args.feature, args.version), indent=2, ensure_ascii=False))
        return 0

    packages = adapter.list_packages(args.feature or None)
    payload: JsonObject = {
        "ok": not adapter.warnings,
        "feature": _normalize_feature(args.feature) if args.feature else None,
        "packages": packages,
        "warnings": adapter.warnings,
    }
    print(json.dumps(payload, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
