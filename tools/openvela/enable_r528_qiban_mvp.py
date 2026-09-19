#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


QIBAN_CONFIGS = [
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_GPS_RECEIVER=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_MAP_SERVICE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_NAV_SERVICE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_SENSOR_BRIDGE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_UI=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VEHICLE_SERVICE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VOICE_SERVICE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_WEATHER_SERVICE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_WIFI_BRIDGE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_AI_AGENT=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_MUSIC_SERVICE=y",
    "CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VIDEO_SERVICE=y",
    "CONFIG_AI_SKILL_OVERSPEED=y",
    "CONFIG_AI_SKILL_LOW_BATTERY=y",
    "CONFIG_AI_SKILL_FATIGUE=y",
    "CONFIG_AI_SKILL_NAV_ARRIVAL=y",
    "CONFIG_AI_SKILL_NAV_TURN=y",
    "CONFIG_AI_SKILL_SPEED_TREND=y",
    "CONFIG_AI_SKILL_RIDE_SUMMARY=y",
    "CONFIG_AI_SKILL_EMERGENCY_STOP=y",
]

QIBAN_RUNTIME_CONFIGS = [
    "CONFIG_LV_FONT_SIMSUN_16_CJK=y",
    "CONFIG_LV_USE_IME_PINYIN=y",
    "CONFIG_NETINIT_NOMAC=y",
    "CONFIG_NETINIT_SWMAC=y",
    "CONFIG_NETINIT_MACADDR_1=0x12345678",
    "CONFIG_NETINIT_MACADDR_2=0x00000200",
    "CONFIG_DRIVERS_GPADC_CTL_NUM=1",
]

QIBAN_REQUIRED_CONFIGS = QIBAN_CONFIGS + QIBAN_RUNTIME_CONFIGS

BOARD_REL = Path("vendor/allwinnertech/boards/r528/r528s3-gemini-s1")
DEFCONFIG_REL = BOARD_REL / "configs/nsh_minidisplay/defconfig"
RCS_REL = BOARD_REL / "src/etc/init.d/rcS.nsh"

QIBAN_RCS_BLOCK = """#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VEHICLE_SERVICE
qiban_vehicle_service serve &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_NAV_SERVICE
qiban_nav_service watch &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_GPS_RECEIVER
qiban_gps_receiver &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_WIFI_BRIDGE
qiban_wifi_bridge &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_AI_AGENT
qiban_ai &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE
qiban_ota_service &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_MUSIC_SERVICE
qiban_music_service &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_WEATHER_SERVICE
qiban_weather_service serve 900 &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VIDEO_SERVICE
qiban_video_service &
#endif

#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_UI
qiban_ui &
#elif defined(CONFIG_LUNCHER_MINI_APP)
luncher_mini &
#endif"""

QIBAN_RCS_REQUIRED = [
    "qiban_vehicle_service serve &",
    "qiban_nav_service watch &",
    "qiban_gps_receiver &",
    "qiban_wifi_bridge &",
    "qiban_ai &",
    "qiban_ota_service &",
    "qiban_music_service &",
    "qiban_weather_service serve 900 &",
    "qiban_video_service &",
    "qiban_ui &",
]

QIBAN_BT_RCS_BLOCK = """#ifdef CONFIG_BT_START
echo "Starting bt_start..."
bt_start &
#endif"""

QIBAN_RCS_SERVICE_BLOCKS = [
    """#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_GPS_RECEIVER
qiban_gps_receiver &
#endif""",
    """#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_WIFI_BRIDGE
qiban_wifi_bridge &
#endif""",
    """#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_OTA_SERVICE
qiban_ota_service &
#endif""",
    """#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VIDEO_SERVICE
qiban_video_service &
#endif""",
]


def default_openvela_root() -> Path:
    return Path(__file__).resolve().parents[3]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")


def update_defconfig(path: Path) -> bool:
    text = read_text(path)
    lines = text.splitlines()
    changed = False

    config_by_name = {
        item.split("=", 1)[0]: item
        for item in QIBAN_REQUIRED_CONFIGS
    }
    found = set()
    updated_lines = []

    for line in lines:
        name = None
        if line.startswith("CONFIG_"):
            name = line.split("=", 1)[0]
        elif line.startswith("# CONFIG_") and line.endswith(" is not set"):
            name = line[len("# "): -len(" is not set")]

        if name in config_by_name:
            found.add(name)
            if line != config_by_name[name]:
                updated_lines.append(config_by_name[name])
                changed = True
            else:
                updated_lines.append(line)
        else:
            updated_lines.append(line)

    lines = updated_lines
    missing = [
        config
        for name, config in config_by_name.items()
        if name not in found
    ]
    if not missing and not changed:
        return False

    insert_at = None
    for idx, line in enumerate(lines):
        if line.startswith("CONFIG_LV_"):
            insert_at = idx
            break

    if insert_at is None:
        insert_at = len(lines)

    lines[insert_at:insert_at] = missing
    write_text(path, "\n".join(lines) + "\n")
    return True


def update_rcs(path: Path) -> bool:
    text = read_text(path)
    changed = False

    if "bluetoothd &" in text:
        old = 'echo "Starting bluetoothd..."\nbluetoothd &'
        if old in text:
            text = text.replace(old, QIBAN_BT_RCS_BLOCK, 1)
        else:
            text = text.replace("bluetoothd &", QIBAN_BT_RCS_BLOCK, 1)
        changed = True
    elif "bt_start &" not in text:
        marker = "#ifndef CONFIG_LCD_DEV"
        if marker in text:
            text = text.replace(marker, QIBAN_BT_RCS_BLOCK + "\n\n" + marker, 1)
        else:
            text = text.rstrip() + "\n\n" + QIBAN_BT_RCS_BLOCK + "\n"
        changed = True

    if "qiban_vehicle_service serve &" in text and "qiban_ui &" in text:
        missing_blocks = [
            block
            for block in QIBAN_RCS_SERVICE_BLOCKS
            if block.splitlines()[1] not in text
        ]
        if not missing_blocks:
            if changed:
                write_text(path, text)
            return changed

        marker = "#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_UI"
        insert = "\n\n".join(missing_blocks) + "\n\n"
        if marker in text:
            text = text.replace(marker, insert + marker, 1)
        else:
            text = text.rstrip() + "\n\n" + insert

        write_text(path, text)
        return True

    launcher_block = """#ifdef CONFIG_LUNCHER_MINI_APP
luncher_mini &
#endif"""

    if launcher_block in text:
        text = text.replace(launcher_block, QIBAN_RCS_BLOCK)
    elif 'echo "Boot nsh ok"' in text:
        text = text.replace('echo "Boot nsh ok"', QIBAN_RCS_BLOCK + '\n\n' + 'echo "Boot nsh ok"')
    else:
        text = text.rstrip() + "\n\n" + QIBAN_RCS_BLOCK + "\n"

    write_text(path, text)
    return True


def check_file(path: Path, required: list[str]) -> list[str]:
    text = read_text(path)
    return [item for item in required if item not in text]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Enable the Qiban R528 nsh_minidisplay MVP in a local openvela tree."
    )
    parser.add_argument(
        "--openvela-root",
        type=Path,
        default=default_openvela_root(),
        help="Path to the openvela workspace root. Defaults to this repo's parent directory.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Only check whether the openvela tree is already configured.",
    )
    args = parser.parse_args()

    root = args.openvela_root.resolve()
    defconfig = root / DEFCONFIG_REL
    rcs = root / RCS_REL

    missing_paths = [str(path) for path in (defconfig, rcs) if not path.exists()]
    if missing_paths:
        print("missing required openvela files:")
        for path in missing_paths:
            print(f"  {path}")
        return 2

    if args.check:
        missing_defconfig = check_file(defconfig, QIBAN_REQUIRED_CONFIGS)
        missing_rcs = check_file(rcs, QIBAN_RCS_REQUIRED)
        if missing_defconfig or missing_rcs:
            print("Qiban R528 MVP is not fully enabled.")
            for item in missing_defconfig:
                print(f"missing defconfig: {item}")
            for item in missing_rcs:
                print(f"missing rcS: {item}")
            return 1
        print("Qiban R528 MVP is already enabled.")
        return 0

    changed_defconfig = update_defconfig(defconfig)
    changed_rcs = update_rcs(rcs)

    if changed_defconfig:
        print(f"updated {defconfig}")
    else:
        print(f"already configured {defconfig}")

    if changed_rcs:
        print(f"updated {rcs}")
    else:
        print(f"already configured {rcs}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
