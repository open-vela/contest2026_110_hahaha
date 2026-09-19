#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  qiban_voice_local_loop.sh [--adb-serial <serial>] [--workdir <dir>]
                            [--poll-interval <seconds>] [--play-after-import]
                            [--asr-only | --tts-only] [--once]

What it does:
  1. Polls board-side ASR and TTS request files over adb.
  2. When a new listen request appears, runs the local Vosk ASR worker.
  3. When a new speak / announce-nav request appears, runs the local Edge TTS worker.
  4. Sends recognized text or synthesized PCM back to the board automatically.

Typical flow:
  Terminal A:
    ./tools/audio/qiban_voice_local_loop.sh --adb-serial <device_serial> --play-after-import

  Board serial:
    qiban_voice_service listen 4
    qiban_voice_service speak 前方右转
    qiban_voice_service announce-nav
EOF
}

ADB_SERIAL=""
WORKDIR="/tmp/qiban_voice_loop"
POLL_INTERVAL_S="1"
PLAY_AFTER_IMPORT=0
ASR_ENABLED=1
TTS_ENABLED=1
RUN_ONCE=0

BOARD_ASR_REQUEST="/data/qiban_voice/asr/last_request.json"
BOARD_TTS_STATUS="/data/qiban_voice_last_tts.json"

log() {
  printf '[%s] %s\n' "$(date '+%H:%M:%S')" "$*"
}

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

json_get_string() {
  local key="$1"
  local path="$2"
  sed -n "s/.*\"${key}\":[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p" "$path" | head -n 1
}

file_signature() {
  local path="$1"
  cksum "$path" | awk '{print $1 ":" $2}'
}

fetch_board_file() {
  local board_path="$1"
  local local_path="$2"
  local tmp_path="${local_path}.tmp"

  mkdir -p "$(dirname "$local_path")"
  if ! adb_cmd shell "cat $(shell_quote "$board_path")" >"${tmp_path}" 2>/dev/null; then
    rm -f "${tmp_path}"
    return 1
  fi

  if [[ ! -s "${tmp_path}" ]]; then
    rm -f "${tmp_path}"
    return 1
  fi

  mv -f "${tmp_path}" "${local_path}"
  return 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --adb-serial)
      ADB_SERIAL="${2:-}"
      shift 2
      ;;
    --workdir)
      WORKDIR="${2:-}"
      shift 2
      ;;
    --poll-interval)
      POLL_INTERVAL_S="${2:-}"
      shift 2
      ;;
    --play-after-import)
      PLAY_AFTER_IMPORT=1
      shift
      ;;
    --asr-only)
      ASR_ENABLED=1
      TTS_ENABLED=0
      shift
      ;;
    --tts-only)
      ASR_ENABLED=0
      TTS_ENABLED=1
      shift
      ;;
    --once)
      RUN_ONCE=1
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

if ! command -v adb >/dev/null 2>&1; then
  echo "adb is required." >&2
  exit 1
fi

mkdir -p "${WORKDIR}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAST_ASR_SIGNATURE=""
LAST_TTS_SIGNATURE=""

log "Polling board voice requests in ${WORKDIR}"
if [[ "${ASR_ENABLED}" -eq 1 ]]; then
  log "ASR bridge enabled"
fi
if [[ "${TTS_ENABLED}" -eq 1 ]]; then
  log "TTS bridge enabled"
fi

while true; do
  if [[ "${ASR_ENABLED}" -eq 1 ]]; then
    ASR_REQUEST_PATH="${WORKDIR}/last_asr_request.json"
    if fetch_board_file "${BOARD_ASR_REQUEST}" "${ASR_REQUEST_PATH}"; then
      ASR_STATUS="$(json_get_string status "${ASR_REQUEST_PATH}")"
      ASR_SIGNATURE="$(file_signature "${ASR_REQUEST_PATH}")"
      if [[ "${ASR_STATUS}" == "asr_request_prepared" && "${ASR_SIGNATURE}" != "${LAST_ASR_SIGNATURE}" ]]; then
        ASR_CMD=("${SCRIPT_DIR}/qiban_asr_local_worker.sh" --board-request "${BOARD_ASR_REQUEST}")
        if [[ -n "${ADB_SERIAL}" ]]; then
          ASR_CMD+=(--adb-serial "${ADB_SERIAL}")
        fi

        log "New ASR request detected"
        if "${ASR_CMD[@]}"; then
          LAST_ASR_SIGNATURE="${ASR_SIGNATURE}"
          log "ASR request completed"
        else
          log "ASR worker failed, will retry"
        fi
      fi
    fi
  fi

  if [[ "${TTS_ENABLED}" -eq 1 ]]; then
    TTS_STATUS_PATH="${WORKDIR}/last_tts_status.json"
    if fetch_board_file "${BOARD_TTS_STATUS}" "${TTS_STATUS_PATH}"; then
      TTS_STATUS="$(json_get_string status "${TTS_STATUS_PATH}")"
      TTS_SIGNATURE="$(file_signature "${TTS_STATUS_PATH}")"
      if [[ ("${TTS_STATUS}" == "tts_request_queued" || "${TTS_STATUS}" == "tts_placeholder_playback_prepared") && "${TTS_SIGNATURE}" != "${LAST_TTS_SIGNATURE}" ]]; then
        TTS_CMD=("${SCRIPT_DIR}/qiban_tts_local_worker.sh")
        if [[ -n "${ADB_SERIAL}" ]]; then
          TTS_CMD+=(--adb-serial "${ADB_SERIAL}")
        fi
        if [[ "${PLAY_AFTER_IMPORT}" -eq 1 ]]; then
          TTS_CMD+=(--play-after-import)
        fi

        log "New TTS request detected"
        if "${TTS_CMD[@]}"; then
          LAST_TTS_SIGNATURE="${TTS_SIGNATURE}"
          log "TTS request completed"
        else
          log "TTS worker failed, will retry"
        fi
      fi
    fi
  fi

  if [[ "${RUN_ONCE}" -eq 1 ]]; then
    break
  fi

  sleep "${POLL_INTERVAL_S}"
done
