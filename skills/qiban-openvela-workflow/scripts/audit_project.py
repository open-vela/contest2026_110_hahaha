#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Audit the Qiban contest repository")
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--openvela-root", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    root = args.openvela_root.resolve() if args.openvela_root else repo.parent
    errors: list[str] = []
    warnings: list[str] = []

    required = [
        "README.md",
        "contest2026_110_hahaha.xml",
        "tools/openvela/enable_r528_qiban_mvp.py",
        "logs/README.md",
    ]
    for rel in required:
        if not (repo / rel).is_file():
            errors.append(f"missing required file: {rel}")

    manifest_path = repo / "contest2026_110_hahaha.xml"
    linked_sources: set[str] = set()
    if manifest_path.is_file():
        try:
            tree = ET.parse(manifest_path)
            linked_sources = {
                node.attrib["src"]
                for node in tree.findall(".//linkfile")
                if "src" in node.attrib
            }
        except ET.ParseError as exc:
            errors.append(f"invalid manifest XML: {exc}")

    app_dirs = sorted(
        path.relative_to(repo).as_posix()
        for path in (repo / "app").glob("qiban_*")
        if path.is_dir()
    )
    for rel in app_dirs:
        if rel not in linked_sources:
            errors.append(f"native module is not linked by manifest: {rel}")

    if (repo / "logs/your-github-login").exists():
        errors.append("template AI log directory still exists")

    real_logs = list((repo / "logs").glob("*/*/*.jsonl"))
    if not real_logs:
        warnings.append("no exported AI Coding JSONL sessions found")
    for path in real_logs:
        try:
            with path.open(encoding="utf-8") as handle:
                for number, line in enumerate(handle, 1):
                    json.loads(line)
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"invalid AI log {path.relative_to(repo)}:{number}: {exc}")

    ignored_artifacts = [
        repo / "quickapp/qiban_ai_dashboard/node_modules",
        repo / "quickapp/qiban_ai_dashboard/build",
        repo / "quickapp/qiban_ai_dashboard/dist",
        repo / "tools/map_assets/build",
    ]
    for path in ignored_artifacts:
        if path.exists():
            warnings.append(f"local generated artifact exists; keep it ignored: {path.relative_to(repo)}")

    defconfig = root / "vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/defconfig"
    if defconfig.is_file():
        text = defconfig.read_text(encoding="utf-8", errors="replace")
        enabled = sorted(set(re.findall(r"^CONFIG_(LVX_USE_DEMO_CONTEST2026_110_QIBAN_[A-Z0-9_]+)=y$", text, re.M)))
        print(f"enabled_qiban_modules={len(enabled)}")
        for symbol in enabled:
            print(f"  CONFIG_{symbol}=y")
    else:
        warnings.append(f"openvela defconfig not found: {defconfig}")

    for message in warnings:
        print(f"WARNING: {message}")
    for message in errors:
        print(f"ERROR: {message}")

    print(f"app_modules={len(app_dirs)}")
    print(f"ai_log_sessions={len(real_logs)}")
    print(f"errors={len(errors)} warnings={len(warnings)}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
