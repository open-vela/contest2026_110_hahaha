#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ ! -d "${SCRIPT_DIR}/.venv" ]]; then
  python3 -m venv "${SCRIPT_DIR}/.venv"
fi

# shellcheck source=/dev/null
source "${SCRIPT_DIR}/.venv/bin/activate"

python3 -m pip install -r "${SCRIPT_DIR}/requirements.txt"
exec uvicorn app.main:app \
  --app-dir "${SCRIPT_DIR}" \
  --host "${QIBAN_SERVER_HOST:-0.0.0.0}" \
  --port "${QIBAN_SERVER_PORT:-8787}"
