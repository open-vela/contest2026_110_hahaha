#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEFAULT_GRAPH="${REPO_ROOT}/third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_demo/map_demo/assets/nav/road_graph.bin"

usage() {
  cat <<'EOF'
Usage:
  stage_offline_assets.sh --sdcard-root <path> [--tile-root <path>] [--graph <path>]

Purpose:
  Stage offline map assets into an SD card directory layout expected by qiban_ui:
    <sdcard-root>/map/<zoom>/<x>/<y>/tile.png
    <sdcard-root>/nav/road_graph.bin

Options:
  --sdcard-root   Target SD card mount root or staging directory.
  --tile-root     Existing offline tile root to copy into <sdcard-root>/map.
  --graph         Custom road_graph.bin path. Defaults to bundled demo graph.

Notes:
  - This script does not download tiles.
  - Tiles must already use the filename tile.png for the current qiban_ui config.
EOF
}

SDCARD_ROOT=""
TILE_ROOT=""
GRAPH_PATH="${DEFAULT_GRAPH}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --sdcard-root)
      SDCARD_ROOT="${2:-}"
      shift 2
      ;;
    --tile-root)
      TILE_ROOT="${2:-}"
      shift 2
      ;;
    --graph)
      GRAPH_PATH="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "${SDCARD_ROOT}" ]]; then
  echo "--sdcard-root is required" >&2
  usage >&2
  exit 1
fi

if [[ ! -f "${GRAPH_PATH}" ]]; then
  echo "road_graph.bin not found: ${GRAPH_PATH}" >&2
  exit 1
fi

mkdir -p "${SDCARD_ROOT}/nav"
cp -f "${GRAPH_PATH}" "${SDCARD_ROOT}/nav/road_graph.bin"
echo "Staged graph: ${SDCARD_ROOT}/nav/road_graph.bin"

if [[ -n "${TILE_ROOT}" ]]; then
  if [[ ! -d "${TILE_ROOT}" ]]; then
    echo "Tile root not found: ${TILE_ROOT}" >&2
    exit 1
  fi

  mkdir -p "${SDCARD_ROOT}/map"
  cp -a "${TILE_ROOT}/." "${SDCARD_ROOT}/map/"
  echo "Staged tiles: ${TILE_ROOT} -> ${SDCARD_ROOT}/map"

  if ! find "${SDCARD_ROOT}/map" -type f -name 'tile.png' | grep -q .; then
    echo "Warning: no tile.png found under ${SDCARD_ROOT}/map" >&2
    echo "Current qiban_ui expects tile filename tile.png" >&2
  fi
else
  echo "Tiles not staged: pass --tile-root when you have an offline tile directory"
fi
