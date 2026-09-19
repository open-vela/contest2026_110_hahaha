#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "${SCRIPT_DIR}/qiban_asr_worker.sh" \
  --asr-script "${SCRIPT_DIR}/qiban_asr_local.sh" \
  "$@"
