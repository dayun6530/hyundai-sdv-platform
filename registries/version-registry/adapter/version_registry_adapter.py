#!/usr/bin/env python3
"""Read-only Primary/Candidate view over the legacy version/package registry files.

This prototype intentionally does not modify any existing deployment files. It reads:

- sdv-ota-manager/platform/deploy/version_map.json
- sdv-ota-manager/platform/deploy/package_registry.json
- sdv-ota-manager/platform/deploy/packages/*/*/manifest.json

and exposes the data using Primary/Candidate terminology.
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


def _unique_sorted_versions(values: Iterable[Any]) -> List[str]:
    seen: Dict[str, str] = {}
    for value in values:
        version = str(value or "").strip()
        if not version:
            continue
        key = _version_key(version)
        if key not in seen:
            seen[key] = version
    return [seen[k] for k in sorted(seen.keys())]


class VersionRegistryAdapter:
    """Read-only adapter from legacy registry files to Primary/Candidate concepts."""

    def __init__(self, root: str | Path = ".") -> None:
        self.root = Path(root).resolve()
        self.deploy_root = self.root / "sdv-ota-manager" / "platform" / "deploy"
        self.version_map_path = self.deploy_root / "version_map.json"
        self.package_registry_path = self.deploy_root / "package_registry.json"
        self.packages_root = self.deploy_root / "packages"

        self._version_map: Optional[JsonObject] = None
        self._package_registry: Optional[JsonObject] = None
        self._manifests: Optional[List[JsonObject]] = None
        self.warnings: List[str] = []

    def load_version_map(self) -> JsonObject:
        data, warning = _read_json_file(self.version_map_path)
        if warning:
            self._add_warning(warning)
            self._version_map = {}
            return {}
        if not isinstance(data, dict):
            self._add_warning(f"version_map is not an object: {self.version_map_path.as_posix()}")
            self._version_map = {}
            return {}
        self._version_map = data
        return data

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

    def list_features(self) -> List[str]:
        features = set()
        version_map = self._get_version_map()
        vm_features = version_map.get("features")
        if isinstance(vm_features, dict):
            for key in vm_features.keys():
                feature = _normalize_feature(key)
                if feature:
                    features.add(feature)

        for package in self._all_packages_and_manifests():
            feature = self._package_feature(package)
            if feature:
                features.add(feature)

        return sorted(features)

    def list_deployable_versions(self, feature: str) -> List[str]:
        target = _normalize_feature(feature)
        versions: List[str] = []

        feature_map = self._legacy_feature_map(target)
        if isinstance(feature_map, dict):
            for entry in feature_map.values():
                if isinstance(entry, dict):
                    versions.append(str(entry.get("version") or ""))

        for package in self._all_packages_and_manifests():
            if self._package_feature(package) == target:
                versions.append(str(package.get("version") or ""))

        return _unique_sorted_versions(versions)

    def get_primary_version(self, feature: str) -> Optional[str]:
        ref = self._version_by_role_or_slot(feature, "stable", "A")
        return str(ref.get("version")) if ref and ref.get("version") else None

    def get_candidate_version(self, feature: str) -> Optional[str]:
        ref = self._version_by_role_or_slot(feature, "candidate", "B")
        return str(ref.get("version")) if ref and ref.get("version") else None

    def get_package_metadata(self, feature: str, version: str) -> Optional[JsonObject]:
        target = _normalize_feature(feature)
        for package in self._all_packages_and_manifests():
            if self._package_feature(package) == target and _version_matches(package.get("version"), version):
                return self._normalize_package_metadata(package)
        return None

    def resolve_image_uri(self, feature: str, version: str) -> Optional[str]:
        metadata = self.get_package_metadata(feature, version)
        if not metadata:
            return None
        image = metadata.get("docker_image")
        return str(image) if image else None

    def is_version_deployable(self, feature: str, version: str) -> bool:
        versions = self.list_deployable_versions(feature)
        if not any(_version_matches(v, version) for v in versions):
            return False

        metadata = self.get_package_metadata(feature, version)
        if not metadata:
            # Legacy executable/package flows may only be represented in version_map.json.
            return True

        artifact_type = str(metadata.get("artifact_type") or "").strip()
        if artifact_type and artifact_type != "docker_image":
            return True

        docker_image = str(metadata.get("docker_image") or "").strip()
        if not docker_image:
            return False

        image_digest = str(metadata.get("image_digest") or "").strip()
        checksum_file = str(metadata.get("checksum_file") or "").strip()
        if image_digest:
            return True
        if not checksum_file:
            return False

        manifest_path = metadata.get("manifest_path_abs")
        if manifest_path:
            checksum_path = Path(str(manifest_path)).parent / checksum_file
            return checksum_path.exists()

        return True

    def to_primary_candidate_view(self) -> JsonObject:
        versions: JsonObject = {}
        for feature in self.list_features():
            primary = self.get_primary_version(feature)
            candidate = self.get_candidate_version(feature)
            deployable = self.list_deployable_versions(feature)
            note = None
            if primary is None and candidate is None:
                note = "Primary/Candidate mapping is not fully available from legacy version_map.json"

            versions[feature] = {
                "primary_version": primary,
                "candidate_version": candidate,
                "deployable_versions": deployable,
            }
            if note:
                versions[feature]["note"] = note

        return {
            "ok": not self.warnings,
            "root": self.root.as_posix(),
            "deploy_root": self.deploy_root.as_posix(),
            "features": self.list_features(),
            "versions": versions,
            "warnings": self.warnings,
        }

    def _add_warning(self, message: str) -> None:
        if message not in self.warnings:
            self.warnings.append(message)

    def _get_version_map(self) -> JsonObject:
        if self._version_map is None:
            return self.load_version_map()
        return self._version_map

    def _get_package_registry(self) -> JsonObject:
        if self._package_registry is None:
            return self.load_package_registry()
        return self._package_registry

    def _legacy_feature_map(self, feature: str) -> JsonObject:
        version_map = self._get_version_map()
        features = version_map.get("features")
        if not isinstance(features, dict):
            return {}
        target = _normalize_feature(feature)
        for key, value in features.items():
            if _normalize_feature(key) == target and isinstance(value, dict):
                return value
        return {}

    def _version_by_role_or_slot(self, feature: str, role: str, fallback_slot: str) -> Optional[JsonObject]:
        feature_map = self._legacy_feature_map(feature)
        if not feature_map:
            return None

        for slot, entry in feature_map.items():
            if isinstance(entry, dict) and str(entry.get("role") or "").strip().lower() == role:
                return {**entry, "legacy_slot": slot}

        entry = feature_map.get(fallback_slot)
        if isinstance(entry, dict):
            return {**entry, "legacy_slot": fallback_slot}
        return None

    def _all_packages_and_manifests(self) -> List[JsonObject]:
        packages: List[JsonObject] = []
        registry = self._get_package_registry()
        registry_packages = registry.get("packages")
        if isinstance(registry_packages, list):
            packages.extend(p for p in registry_packages if isinstance(p, dict))
        packages.extend(self._load_manifests())
        return packages

    def _load_manifests(self) -> List[JsonObject]:
        if self._manifests is not None:
            return self._manifests

        manifests: List[JsonObject] = []
        if not self.packages_root.exists():
            self._add_warning(f"missing packages directory: {self.packages_root.as_posix()}")
            self._manifests = []
            return []

        for path in self.packages_root.glob("*/*/manifest.json"):
            data, warning = _read_json_file(path)
            if warning:
                self._add_warning(warning)
                continue
            if not isinstance(data, dict):
                self._add_warning(f"manifest is not an object: {path.as_posix()}")
                continue
            feature_from_path = path.parent.parent.name
            version_from_path = path.parent.name
            enriched = {
                **data,
                "_manifest_path_abs": path.as_posix(),
                "_feature_from_path": feature_from_path,
                "_version_from_path": version_from_path,
            }
            manifests.append(enriched)

        self._manifests = manifests
        return manifests

    def _package_feature(self, package: JsonObject) -> str:
        return _normalize_feature(
            package.get("feature")
            or package.get("domain")
            or package.get("_feature_from_path")
        )

    def _normalize_package_metadata(self, package: JsonObject) -> JsonObject:
        version = package.get("version") or package.get("_version_from_path")
        manifest_path_abs = package.get("_manifest_path_abs")
        return {
            "package_id": package.get("package_id"),
            "feature": self._package_feature(package),
            "version": version,
            "role": package.get("role") or package.get("default_role") or package.get("slot_role"),
            "artifact_type": package.get("artifact_type") or package.get("package_type"),
            "docker_image": package.get("docker_image"),
            "image_digest": package.get("image_digest"),
            "image_digest_source": package.get("image_digest_source"),
            "checksum_file": package.get("checksum_file"),
            "checksum": package.get("checksum"),
            "binary": package.get("binary") or package.get("binary_name") or package.get("executable_name"),
            "compatible_models": package.get("compatible_models") or [],
            "compatible_regions": package.get("compatible_regions") or [],
            "package_path": package.get("package_path"),
            "manifest_path": package.get("manifest_path"),
            "manifest_path_abs": manifest_path_abs,
            "status": package.get("status"),
            "description": package.get("description"),
        }


def main() -> int:
    parser = argparse.ArgumentParser(description="Read-only Primary/Candidate view over legacy version registry files.")
    parser.add_argument("--root", default=".", help="Repository root path (default: current directory)")
    parser.add_argument("--summary", action="store_true", help="Print Primary/Candidate summary JSON")
    parser.add_argument("--feature", default="", help="Optional feature filter for summary output")
    args = parser.parse_args()

    adapter = VersionRegistryAdapter(args.root)
    summary = adapter.to_primary_candidate_view()

    if args.feature:
        feature = _normalize_feature(args.feature)
        summary["features"] = [f for f in summary["features"] if f == feature]
        summary["versions"] = {feature: summary.get("versions", {}).get(feature, {
            "primary_version": None,
            "candidate_version": None,
            "deployable_versions": [],
            "note": "feature not found",
        })}

    if args.summary or not args.feature:
        print(json.dumps(summary, indent=2, ensure_ascii=False))
        return 0

    print(json.dumps(summary, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
