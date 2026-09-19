#!/usr/bin/env bash
set -euo pipefail

# qiban_wifi_voice_loop.sh
# Host-side WiFi voice bridge. Replaces USB ADB for ASR/TTS workflows.
# Polls board HTTP bridge for voice requests, processes locally, sends back.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<'EOF'
Usage:
  qiban_wifi_voice_loop.sh --board-ip <ip> [--port <port>]
                           [--poll-interval <sec>] [--play-after-import]
                           [--asr-only | --tts-only] [--once]

Examples:
  # Full bridge (ASR + TTS)
  ./tools/audio/qiban_wifi_voice_loop.sh --board-ip 192.168.1.100 --play-after-import

  # ASR only
  ./tools/audio/qiban_wifi_voice_loop.sh --board-ip 192.168.1.100 --asr-only

  # TTS only
  ./tools/audio/qiban_wifi_voice_loop.sh --board-ip 192.168.1.100 --tts-only --play-after-import

  # Single pass (process current requests then exit)
  ./tools/audio/qiban_wifi_voice_loop.sh --board-ip 192.168.1.100 --once
EOF
}

BOARD_IP=""
BOARD_PORT="8081"
POLL_INTERVAL_S="2"
PLAY_AFTER_IMPORT=0
ASR_ENABLED=1
TTS_ENABLED=1
RUN_ONCE=0
WORKDIR="/tmp/qiban_wifi_voice"

log() {
  printf '[%s] %s\n' "$(date '+%H:%M:%S')" "$*"
}

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --board-ip)    BOARD_IP="${2:-}";           shift 2 ;;
    --port)        BOARD_PORT="${2:-}";         shift 2 ;;
    --poll-interval) POLL_INTERVAL_S="${2:-}";  shift 2 ;;
    --play-after-import) PLAY_AFTER_IMPORT=1;   shift ;;
    --asr-only)    ASR_ENABLED=1; TTS_ENABLED=0; shift ;;
    --tts-only)    ASR_ENABLED=0; TTS_ENABLED=1; shift ;;
    --once)        RUN_ONCE=1;                   shift ;;
    -h|--help)     usage; exit 0 ;;
    *)             echo "Unknown: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [[ -z "$BOARD_IP" ]]; then
  echo "Error: --board-ip is required" >&2
  echo "Run: ifconfig   on the board to find its IP" >&2
  exit 1
fi

BASE_URL="http://${BOARD_IP}:${BOARD_PORT}"
WORKDIR_ASR="${WORKDIR}/asr"
WORKDIR_TTS="${WORKDIR}/tts"

mkdir -p "$WORKDIR_ASR" "$WORKDIR_TTS"

log "WiFi voice bridge starting"
log "Board: ${BASE_URL}"
log "ASR: ${ASR_ENABLED}  TTS: ${TTS_ENABLED}"

# Track last processed state to avoid re-processing
LAST_ASR_SIG=""
LAST_TTS_SIG=""

file_sig() {
  cksum "$1" 2>/dev/null | awk '{print $1 ":" $2}' || echo ""
}

do_curl() {
  # Quiet curl with error handling
  local output="$1"; shift
  if ! curl -sf "$@" -o "$output" 2>/dev/null; then
    return 1
  fi
  return 0
}

# ============================================================
# ASR flow: board -> host -> board
# ============================================================
process_asr() {
  local req_file="${WORKDIR_ASR}/last_request.json"

  # 1. Get current ASR request from board
  if ! do_curl "$req_file" "${BASE_URL}/api/asr/request"; then
    return 0  # No request or board not ready
  fi

  if [[ ! -s "$req_file" ]]; then
    return 0
  fi

  # 2. Check if this is a new request
  local sig
  sig=$(file_sig "$req_file")
  if [[ "$sig" == "$LAST_ASR_SIG" ]]; then
    return 0
  fi

  # 3. Check status
  local status
  status=$(sed -n 's/.*"status":[[:space:]]*"\([^"]*\)".*/\1/p' "$req_file" | head -1)
  if [[ "$status" != "asr_request_prepared" ]]; then
    return 0
  fi

  # 4. Extract PCM path
  local pcm_path
  pcm_path=$(sed -n 's/.*"pcm_path":[[:space:]]*"\([^"]*\)".*/\1/p' "$req_file" | head -1)
  if [[ -z "$pcm_path" ]]; then
    log "ASR: no pcm_path in request"
    return 0
  fi

  log "ASR: new request, pcm=${pcm_path}"

  # 5. Download PCM from board
  local local_pcm="${WORKDIR_ASR}/$(basename "$pcm_path")"
  if ! do_curl "$local_pcm" "${BASE_URL}/api/file?path=${pcm_path}"; then
    log "ASR: failed to download PCM"
    return 1
  fi

  if [[ ! -s "$local_pcm" ]]; then
    log "ASR: downloaded PCM is empty"
    return 1
  fi

  log "ASR: PCM downloaded ($(wc -c < "$local_pcm") bytes)"

  # 6. Run local ASR
  local recognized=""
  if [[ -x "${SCRIPT_DIR}/qiban_asr_local.sh" ]]; then
    recognized=$("${SCRIPT_DIR}/qiban_asr_local.sh" "$local_pcm" 2>/dev/null | \
                 tr '\n' ' ' | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')
  else
    log "ASR: qiban_asr_local.sh not found or not executable"
    return 1
  fi

  if [[ -z "$recognized" ]]; then
    log "ASR: recognition returned empty"
    return 0
  fi

  log "ASR: recognized -> ${recognized}"

  # 7. Send result back to board
  local payload
  payload=$(printf '{"text":"%s"}' "$recognized")
  if curl -sf -X POST \
    -H "Content-Type: application/json" \
    -d "$payload" \
    "${BASE_URL}/api/asr/result" \
    -o /dev/null 2>/dev/null; then
    log "ASR: result sent to board"
    LAST_ASR_SIG="$sig"
  else
    log "ASR: failed to send result"
    return 1
  fi
}

# ============================================================
# TTS flow: board -> host -> board
# ============================================================
process_tts() {
  local req_file="${WORKDIR_TTS}/last_request.txt"

  # 1. Get current TTS request from board
  if ! do_curl "$req_file" "${BASE_URL}/api/tts/request"; then
    return 0
  fi

  if [[ ! -s "$req_file" ]]; then
    return 0
  fi

  # 2. Check if new
  local sig
  sig=$(file_sig "$req_file")
  if [[ "$sig" == "$LAST_TTS_SIG" ]]; then
    return 0
  fi

  local text
  text=$(cat "$req_file" | tr -d '\n\r')
  if [[ -z "$text" ]]; then
    return 0
  fi

  log "TTS: new request -> ${text}"

  # 3. Run local TTS
  local local_pcm="${WORKDIR_TTS}/tts_output.pcm"
  if [[ -x "${SCRIPT_DIR}/qiban_tts_local.sh" ]]; then
    if ! "${SCRIPT_DIR}/qiban_tts_local.sh" \
      --text "$text" \
      --output "$local_pcm" >/dev/null 2>&1; then
      log "TTS: synthesis failed"
      return 1
    fi
  else
    log "TTS: qiban_tts_local.sh not found or not executable"
    return 1
  fi

  if [[ ! -s "$local_pcm" ]]; then
    log "TTS: synthesized PCM is empty"
    return 1
  fi

  log "TTS: PCM synthesized ($(wc -c < "$local_pcm") bytes)"

  # 4. Upload PCM to board
  if curl -sf -X POST \
    -H "Content-Type: application/octet-stream" \
    --data-binary "@${local_pcm}" \
    "${BASE_URL}/api/tts/upload" \
    -o /dev/null 2>/dev/null; then
    log "TTS: PCM uploaded to board"
    LAST_TTS_SIG="$sig"
  else
    log "TTS: failed to upload PCM"
    return 1
  fi

  # 5. Trigger playback if requested
  if [[ "$PLAY_AFTER_IMPORT" -eq 1 ]]; then
    local play_cmd="qiban_voice_service play /data/qiban_voice/tts/last_tts_16k_s16_mono.pcm"
    local play_payload
    play_payload=$(printf '{"cmd":"%s"}' "$play_cmd")
    if curl -sf -X POST \
      -H "Content-Type: application/json" \
      -d "$play_payload" \
      "${BASE_URL}/api/command" \
      -o /dev/null 2>/dev/null; then
      log "TTS: playback triggered"
    else
      log "TTS: failed to trigger playback"
    fi
  fi
}

# ============================================================
# Main loop
# ============================================================

# Verify board is reachable
if ! curl -sf "${BASE_URL}/api/status" -o /dev/null 2>/dev/null; then
  log "ERROR: Board not reachable at ${BASE_URL}"
  log "Make sure qiban_wifi_bridge is running on the board"
  exit 1
fi

log "Board connected, starting poll loop..."

while true; do
  if [[ "$ASR_ENABLED" -eq 1 ]]; then
    process_asr || true
  fi

  if [[ "$TTS_ENABLED" -eq 1 ]]; then
    process_tts || true
  fi

  if [[ "$RUN_ONCE" -eq 1 ]]; then
    break
  fi

  sleep "$POLL_INTERVAL_S"
done

log "Done."
