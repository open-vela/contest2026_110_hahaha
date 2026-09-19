#!/usr/bin/env python3
"""Build an application OTA package for the Qiban AI dashboard.

Usage:
    python3 build_ota.py --version 1.1.0 --out-dir ./ota_output

This script:
1. Reads the latest built binaries from the openvela build output
2. Computes SHA-256 for each component
3. Generates manifest.json
4. Packages everything into app_ota.zip
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import zipfile
from datetime import datetime, timezone
from pathlib import Path

# Components to include in the OTA package
COMPONENTS = [
    "qiban_ui",
    "qiban_vehicle_service",
    "qiban_nav_service",
    "qiban_map_service",
    "qiban_voice_service",
    "qiban_ai_agent",
    "qiban_sensor_bridge",
    "qiban_gps_receiver",
    "qiban_wifi_bridge",
]

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(8192):
            h.update(chunk)
    return h.hexdigest()


def find_binary(name: str, search_dir: Path) -> Path | None:
    """Find a NuttX application binary in the build output."""
    # Common locations for NuttX app binaries
    candidates = [
        search_dir / name,
        search_dir / f"{name}.elf",
        search_dir / "apps" / name,
        search_dir / "apps" / f"{name}.elf",
    ]
    for c in candidates:
        if c.exists():
            return c
    # Recursive search
    for p in search_dir.rglob(name):
        if p.is_file():
            return p
    return None


def build_ota_package(
    version: str,
    build_dir: Path,
    out_dir: Path,
    min_fw_version: str = "1.0.0",
    changelog: str = "",
) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "version": version,
        "build_time": datetime.now(timezone.utc).isoformat(),
        "min_firmware_version": min_fw_version,
        "changelog": changelog,
        "components": [],
    }

    zip_path = out_dir / "app_ota.zip"
    total_size = 0

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for name in COMPONENTS:
            binary = find_binary(name, build_dir)
            if binary is None:
                print(f"  [skip] {name}: not found in {build_dir}")
                continue

            data = binary.read_bytes()
            sha = hashlib.sha256(data).hexdigest()
            size = len(data)
            total_size += size

            zf.writestr(f"bin/{name}", data)
            manifest["components"].append({
                "name": name,
                "version": version,
                "sha256": sha,
                "size": size,
            })
            print(f"  [ok] {name}: {size} bytes, sha256={sha[:16]}...")

        # Write manifest into the zip
        manifest_json = json.dumps(manifest, indent=2)
        zf.writestr("manifest.json", manifest_json)

    # Compute zip-level hash
    zip_sha = sha256_file(zip_path)
    zip_size = zip_path.stat().st_size
    manifest["sha256"] = zip_sha
    manifest["size"] = zip_size

    # Write manifest alongside zip
    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    print(f"\nOTA package: {zip_path}")
    print(f"  Version: {version}")
    print(f"  Size: {zip_size} bytes")
    print(f"  SHA-256: {zip_sha}")
    print(f"  Components: {len(manifest['components'])}")

    return zip_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Build Qiban app OTA package")
    parser.add_argument("--version", required=True, help="OTA version (e.g. 1.1.0)")
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="openvela build output directory")
    parser.add_argument("--out-dir", type=Path, default=Path("./ota_output"),
                        help="Output directory for OTA package")
    parser.add_argument("--min-fw", default="1.0.0",
                        help="Minimum firmware version required")
    parser.add_argument("--changelog", default="",
                        help="Changelog text")
    args = parser.parse_args()

    if not args.build_dir.exists():
        print(f"Build directory not found: {args.build_dir}")
        return 1

    build_ota_package(
        version=args.version,
        build_dir=args.build_dir,
        out_dir=args.out_dir,
        min_fw_version=args.min_fw,
        changelog=args.changelog,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
