---
name: qiban-openvela-workflow
description: Build, diagnose, verify, and prepare the Qiban AI electric-scooter dashboard on openvela for R528S3-Gemini-S1. Use for this repository's manifest wiring, nsh_minidisplay firmware builds, LVGL/map/navigation/voice service debugging, offline-map deployment, board smoke tests, AI log checks, and contest delivery audits.
---

# Qiban openvela Workflow

Use the repository as the source of truth. Distinguish source presence, firmware inclusion, and board verification in every status report.

## Inspect First

1. Read `README.md`, `contest2026_110_hahaha.xml`, and `tools/openvela/enable_r528_qiban_mvp.py`.
2. Read [architecture.md](references/architecture.md) when changing service boundaries or `/data` contracts.
3. Run `python3 skills/qiban-openvela-workflow/scripts/audit_project.py --repo . --openvela-root ..`.
4. Check `git status --short` before editing. Preserve unrelated user changes.

## Build Firmware

Run from the openvela workspace root:

```bash
python3 contest2026_110_hahaha/tools/openvela/enable_r528_qiban_mvp.py \
  --openvela-root .
./build.sh vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/ \
  distclean -j8
./build.sh vendor/allwinnertech/boards/r528/r528s3-gemini-s1/configs/nsh_minidisplay/ -j8
```

Do not report a module as included merely because its source exists. Confirm its Kconfig symbol in the active `.config` or its entry point in `nuttx/System.map`.

## Debug the Board

Use short ADB/NSH commands. Long serial URLs may be split by the shell.

```bash
adb devices
adb shell 'qiban_vehicle_service once'
adb shell 'qiban_map_service status'
adb shell 'qiban_nav_service status'
adb shell 'cat /data/qiban_location_state.json'
adb shell 'cat /data/qiban_nav_state.json'
```

For offline navigation, push files directly when HTTP routing is uncertain:

```bash
adb push <staged-map-directory> /data/qiban_offline_map/map/
adb push road_graph.bin /data/qiban_offline_map/nav/inbox/road_graph.bin
adb shell 'qiban_map_service offline-graph /data/qiban_offline_map/nav/inbox/road_graph.bin'
```

Validate coordinates before allowing the UI to consume navigation state. Treat missing tiles as data errors; do not assume Wi-Fi is the crash source without a stack or fault address.

## Verify Changes

- Run Python compilation for `server/app` and `tools`.
- Run `bash -n` for shell scripts.
- Parse every JSON file under `shared/`.
- Build the Quick App with `npm run build` when its source changes.
- Run the audit script again and record unresolved warnings.
- For board-facing changes, verify with ADB or serial output when hardware is connected.

## Prepare Contest Delivery

1. Export real AI Coding sessions into `logs/<github-login>/`; never handwrite conversation events.
2. Remove the template log directory.
3. Keep build products, dependencies, map tiles, credentials, and private keys out of Git.
4. Ensure the manifest links every submitted native module.
5. State missing video, photos, release RPK, runtime Skill, or measurements explicitly.
6. Create a topic branch and PR only after the user authorizes the external operation.

Do not claim unmeasured power, latency, stability, accuracy, or model performance.
