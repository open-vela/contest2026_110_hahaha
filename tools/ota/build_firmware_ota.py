#!/usr/bin/env python3
"""Build a firmware OTA package (ota.zip) for the Qiban AI dashboard.

This creates an ota.zip that the R528 bootloader (rcS.blboottee) can
detect at /data/ota.zip, verify, mount as zipfs, and boot into the
OTA recovery image which then applies the firmware update.

ota.zip structure:
    vela_ota.bin     -- OTA recovery NuttX image (BL bootloader image)
    ota.sh           -- Update script executed by rcS.ota
    firmware/
        nsh.fex      -- Main NuttX firmware (bootloader partition)
        res.fex      -- Resource filesystem (res partition, optional)

Usage:
    # After building the firmware:
    python3 build_firmware_ota.py \\
        --version 1.1.0 \\
        --bl-image <path-to-vela_bl.bin-or-nsh.fex> \\
        --res-image <path-to-res.fex> \\
        --out-dir ./firmware_ota_output

    # The ota.zip can also be used for non-recovery direct update:
    python3 build_firmware_ota.py \\
        --version 1.1.0 \\
        --fw-image <path-to-nsh.fex> \\
        --direct \\
        --out-dir ./firmware_ota_output
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import zipfile
from datetime import datetime, timezone
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(8192):
            h.update(chunk)
    return h.hexdigest()


def generate_ota_sh(version: str, has_res: bool) -> str:
    """Generate the ota.sh script that the OTA recovery image runs."""

    lines = [
        "#!/bin/sh",
        "# Firmware OTA update script",
        f"# Generated for version {version}",
        f"# Generated at {datetime.now(timezone.utc).isoformat()}",
        "",
        "set -e",
        "",
        'echo "=== Qiban Firmware OTA Update ==="',
        f'echo "Target version: {version}"',
        'echo ""',
        "",
        "# Progress tracking",
        "setprop ota.progress.current 30",
        "",
        "# ── Step 1: Update bootloader partition (main firmware) ──",
        'echo "Writing firmware to bootloader partition..."',
        "",
        "FW_SRC=/ota/firmware/nsh.fex",
        "FW_DST=/dev/bootloader",
        "FW_OFFSET=0",
        "",
        'if [ ! -e "$FW_SRC" ]; then',
        '    echo "ERROR: firmware image not found: $FW_SRC"',
        "    setprop ota.progress.current -1",
        "    exit 1",
        "fi",
        "",
        "FW_SIZE=$(stat -c %s \"$FW_SRC\")",
        'echo "Firmware size: $FW_SIZE bytes"',
        "",
        "# Write firmware to bootloader partition",
        "dd if=\"$FW_SRC\" of=\"$FW_DST\" bs=65536 2>/dev/null",
        'echo "Firmware written successfully"',
        "",
        "setprop ota.progress.current 70",
        "",
    ]

    if has_res:
        lines += [
            "# ── Step 2: Update resource partition ──",
            'echo "Writing resources to res partition..."',
            "",
            "RES_SRC=/ota/firmware/res.fex",
            "RES_DST=/dev/res",
            "",
            'if [ -e "$RES_SRC" ]; then',
            "    dd if=\"$RES_SRC\" of=\"$RES_DST\" bs=65536 2>/dev/null",
            '    echo "Resources written successfully"',
            "else",
            '    echo "No resource image found, skipping"',
            "fi",
            "",
            "setprop ota.progress.current 90",
            "",
        ]

    lines += [
        "# ── Step 3: Verify ──",
        'echo "Verifying firmware..."',
        "# Basic size check",
        "WRITTEN=$(stat -c %s \"$FW_DST\" 2>/dev/null || echo 0)",
        'if [ "$WRITTEN" -gt 0 ]; then',
        '    echo "Verification passed"',
        "else",
        '    echo "WARNING: Could not verify written size"',
        "fi",
        "",
        "setprop ota.progress.current 100",
        "",
        'echo ""',
        'echo "=== OTA Update Complete ==="',
        'echo "Rebooting in 3 seconds..."',
        "sleep 3",
        "reboot",
    ]

    return "\n".join(lines) + "\n"


def generate_manifest(version: str, bl_path: Path | None,
                      fw_path: Path | None, res_path: Path | None) -> dict:
    manifest = {
        "type": "firmware_ota",
        "version": version,
        "build_time": datetime.now(timezone.utc).isoformat(),
        "components": {},
    }

    if bl_path and bl_path.exists():
        manifest["components"]["vela_ota.bin"] = {
            "sha256": sha256_file(bl_path),
            "size": bl_path.stat().st_size,
        }

    if fw_path and fw_path.exists():
        manifest["components"]["firmware/nsh.fex"] = {
            "sha256": sha256_file(fw_path),
            "size": fw_path.stat().st_size,
        }

    if res_path and res_path.exists():
        manifest["components"]["firmware/res.fex"] = {
            "sha256": sha256_file(res_path),
            "size": res_path.stat().st_size,
        }

    return manifest


def build_recovery_ota(
    version: str,
    bl_image: Path,
    fw_image: Path | None,
    res_image: Path | None,
    out_dir: Path,
) -> Path:
    """Build ota.zip with recovery image (booted by rcS.blboottee)."""

    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / "ota.zip"

    has_res = res_image is not None and res_image.exists()
    ota_sh = generate_ota_sh(version, has_res)

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        # OTA recovery image (the BL bootloader image)
        zf.write(bl_image, "vela_ota.bin")

        # Update script
        zf.writestr("ota.sh", ota_sh)

        # Firmware images
        if fw_image and fw_image.exists():
            zf.write(fw_image, "firmware/nsh.fex")

        if has_res:
            zf.write(res_image, "firmware/res.fex")  # type: ignore

        # Manifest
        manifest = generate_manifest(version, bl_image, fw_image, res_image)
        zf.writestr("manifest.json", json.dumps(manifest, indent=2))

    zip_sha = sha256_file(zip_path)
    zip_size = zip_path.stat().st_size

    # Write metadata alongside zip
    meta = {
        "version": version,
        "type": "firmware_recovery",
        "sha256": zip_sha,
        "size": zip_size,
        "build_time": datetime.now(timezone.utc).isoformat(),
    }
    (out_dir / "firmware_manifest.json").write_text(
        json.dumps(meta, indent=2), encoding="utf-8"
    )

    print(f"\nFirmware OTA package (recovery mode):")
    print(f"  Output: {zip_path}")
    print(f"  Version: {version}")
    print(f"  Size: {zip_size} bytes")
    print(f"  SHA-256: {zip_sha}")
    print(f"  Recovery image: {bl_image.name}")
    if fw_image:
        print(f"  Firmware: {fw_image.name}")
    if has_res:
        print(f"  Resources: {res_image.name}")  # type: ignore
    print(f"\n  Deploy: copy ota.zip to server or /data/ota.zip on device")
    print(f"  Apply:  reboot → BL detects ota.zip → boots recovery → flashes firmware")

    return zip_path


def build_direct_ota(
    version: str,
    fw_image: Path,
    out_dir: Path,
) -> Path:
    """Build firmware package for direct MTD write (no recovery boot).

    This produces a simple binary that qiban_ota_service can
    download and write directly to the bootloader partition.
    """

    out_dir.mkdir(parents=True, exist_ok=True)

    fw_sha = sha256_file(fw_image)
    fw_size = fw_image.stat().st_size

    # Copy firmware with standard name
    out_fw = out_dir / "firmware.bin"
    out_fw.write_bytes(fw_image.read_bytes())

    meta = {
        "version": version,
        "type": "firmware_direct",
        "sha256": fw_sha,
        "size": fw_size,
        "build_time": datetime.now(timezone.utc).isoformat(),
        "partition": "bootloader",
        "partition_device": "/dev/mtdblock1",  # typical for bootloader
    }
    (out_dir / "firmware_manifest.json").write_text(
        json.dumps(meta, indent=2), encoding="utf-8"
    )

    print(f"\nFirmware OTA package (direct mode):")
    print(f"  Output: {out_fw}")
    print(f"  Version: {version}")
    print(f"  Size: {fw_size} bytes")
    print(f"  SHA-256: {fw_sha}")
    print(f"\n  Deploy: place firmware.bin + firmware_manifest.json on server")
    print(f"  Apply:  qiban_ota_service firmware_apply → dd to /dev/bootloader → reboot")

    return out_fw


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build firmware OTA package for Qiban AI (Gemini-S1 / R528)"
    )
    parser.add_argument("--version", required=True, help="Firmware version (e.g. 1.1.0)")
    parser.add_argument("--bl-image", type=Path,
                        help="BL/bootloader NuttX binary (for recovery mode ota.zip)")
    parser.add_argument("--fw-image", type=Path,
                        help="Main firmware image (nsh.fex / vela_nuttx.bin)")
    parser.add_argument("--res-image", type=Path,
                        help="Resource partition image (res.fex)")
    parser.add_argument("--direct", action="store_true",
                        help="Build direct-mode package (no recovery boot)")
    parser.add_argument("--out-dir", type=Path, default=Path("./firmware_ota_output"),
                        help="Output directory")
    args = parser.parse_args()

    if args.direct:
        if not args.fw_image:
            print("--fw-image required for direct mode")
            return 1
        if not args.fw_image.exists():
            print(f"Firmware image not found: {args.fw_image}")
            return 1
        build_direct_ota(args.version, args.fw_image, args.out_dir)
    else:
        if not args.bl_image:
            print("--bl-image required for recovery mode")
            print("  (use --direct for direct MTD write mode)")
            return 1
        if not args.bl_image.exists():
            print(f"BL image not found: {args.bl_image}")
            return 1
        build_recovery_ota(
            args.version, args.bl_image,
            args.fw_image, args.res_image, args.out_dir
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
