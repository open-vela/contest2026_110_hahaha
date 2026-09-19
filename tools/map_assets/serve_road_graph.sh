#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ASSET_DIR="${REPO_ROOT}/third_party/LVGL-Offline-Map/packages/artinchip/lvgl-ui/aic_demo/map_demo/assets/nav"
PORT="${1:-18080}"

if [[ ! -f "${ASSET_DIR}/road_graph.bin" ]]; then
  echo "road_graph.bin not found: ${ASSET_DIR}/road_graph.bin" >&2
  exit 1
fi

cd "${ASSET_DIR}"
echo "Serving ${ASSET_DIR}/road_graph.bin on port ${PORT}"
exec python3 -m http.server "${PORT}" --bind 0.0.0.0
