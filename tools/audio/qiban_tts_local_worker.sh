#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  qiban_tts_local_worker.sh [--adb-serial <serial>] [--board-request <board_txt>]
                            [--request <local_txt>] [--workdir <dir>]
                            [--board-output <board_pcm>] [--output <local_pcm>]
                            [--play-after-import]

What it does:
  1. Reads the board-side TTS request text, or a local request file.
  2. Runs the local Piper TTS model on the host machine.
  3. Produces a normalized 16kHz/16-bit/mono PCM file.
  4. Optionally pushes that PCM back to the board and runs:
     qiban_voice_service import-tts <board_pcm>
EOF
}

ADB_SERIAL=""
BOARD_REQUEST="/data/qiban_voice/tts/last_request.txt"
REQUEST_PATH=""
WORKDIR="/tmp/qiban_tts"
LOCAL_OUTPUT=""
BOARD_OUTPUT="/data/qiban_voice/tts/last_tts_16k_s16_mono.pcm"
PLAY_AFTER_IMPORT=0

adb_cmd() {
  if [[ -n "${ADB_SERIAL}" ]]; then
    adb -s "${ADB_SERIAL}" "$@"
  else
    adb "$@"
  fi
}

shell_quote() {
  printf "'%s'" "${1//\'/\'\"\'\"\'}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --adb-serial)
      ADB_SERIAL="${2:-}"
      shift 2
      ;;
    --board-request)
      BOARD_REQUEST="${2:-}"
      shift 2
      ;;
    --request)
      REQUEST_PATH="${2:-}"
      shift 2
      ;;
    --workdir)
      WORKDIR="${2:-}"
      shift 2
      ;;
    --board-output)
      BOARD_OUTPUT="${2:-}"
      shift 2
      ;;
    --output)
      LOCAL_OUTPUT="${2:-}"
      shift 2
      ;;
    --play-after-import)
      PLAY_AFTER_IMPORT=1
      shift
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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p "${WORKDIR}"

if [[ -z "${REQUEST_PATH}" ]]; then
  if ! command -v adb >/dev/null 2>&1; then
    echo "adb is required when --request is not provided." >&2
    exit 1
  fi

  REQUEST_PATH="${WORKDIR}/last_tts_request.txt"
  adb_cmd shell "cat $(shell_quote "${BOARD_REQUEST}")" > "${REQUEST_PATH}"
fi

if [[ ! -f "${REQUEST_PATH}" ]]; then
  echo "TTS request file not found: ${REQUEST_PATH}" >&2
  exit 1
fi

if [[ -z "${LOCAL_OUTPUT}" ]]; then
  LOCAL_OUTPUT="${WORKDIR}/last_tts_16k_s16_mono.pcm"
fi

"${SCRIPT_DIR}/qiban_tts_local.sh" \
  --request "${REQUEST_PATH}" \
  --output "${LOCAL_OUTPUT}" >/dev/null

echo "TTS request: ${REQUEST_PATH}"
echo "Local PCM: ${LOCAL_OUTPUT}"

if command -v adb >/dev/null 2>&1 && [[ "${REQUEST_PATH}" == "${WORKDIR}/last_tts_request.txt" ]]; then
  adb_cmd push "${LOCAL_OUTPUT}" "${BOARD_OUTPUT}" >/dev/null
  adb_cmd shell "qiban_voice_service import-tts $(shell_quote "${BOARD_OUTPUT}")"
  if [[ "${PLAY_AFTER_IMPORT}" -eq 1 ]]; then
    adb_cmd shell "qiban_voice_service play $(shell_quote "/data/qiban_voice/tts/last_tts_16k_s16_mono.pcm")"
  fi
  echo "Returned synthesized PCM to board."
else
  echo "Board callback not executed automatically."
  echo "Run on board:"
  echo "  qiban_voice_service import-tts $(shell_quote "${BOARD_OUTPUT}")"
fi
