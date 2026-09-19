#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  qiban_server_voice_loop.sh --server-url <url> [--workdir <dir>]
                             [--poll-interval <seconds>] [--asr-only | --tts-only]
                             [--once]

What it does:
  1. Polls relay server pending queues instead of adb files.
  2. Runs local ASR/TTS workers for claimed jobs.
  3. Pushes results back to the relay server.
EOF
}

SERVER_URL=""
WORKDIR="/tmp/qiban_server_voice_loop"
POLL_INTERVAL_S="1"
ASR_ENABLED=1
TTS_ENABLED=1
RUN_ONCE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --server-url)
      SERVER_URL="${2:-}"
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

if [[ -z "${SERVER_URL}" ]]; then
  echo "--server-url is required." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "${WORKDIR}"

log() {
  printf '[%s] %s\n' "$(date '+%H:%M:%S')" "$*"
}

log "Polling relay server ${SERVER_URL}"

while true; do
  if [[ "${ASR_ENABLED}" -eq 1 ]]; then
    if ! "${SCRIPT_DIR}/qiban_server_asr_local_worker.sh" \
      --server-url "${SERVER_URL}" \
      --workdir "${WORKDIR}/asr"; then
      log "ASR worker returned non-zero"
    fi
  fi

  if [[ "${TTS_ENABLED}" -eq 1 ]]; then
    if ! "${SCRIPT_DIR}/qiban_server_tts_local_worker.sh" \
      --server-url "${SERVER_URL}" \
      --workdir "${WORKDIR}/tts"; then
      log "TTS worker returned non-zero"
    fi
  fi

  if [[ "${RUN_ONCE}" -eq 1 ]]; then
    break
  fi

  sleep "${POLL_INTERVAL_S}"
done
