#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  qiban_asr_worker.sh [--adb-serial <serial>] [--board-request <board_json>] [--workdir <dir>]
                      [--request <local_json>] [--local-pcm <pcm_path>]
                      [--recognized-text <text> | --recognized-text-file <path> | --asr-script <script>]
                      [--import-command <cmd>]

What it does:
  1. Loads the board-side ASR request JSON.
  2. Pulls the recorded PCM from the board, or uses a local PCM path.
  3. Gets recognized Chinese text from one of three sources:
     - --recognized-text
     - --recognized-text-file
     - --asr-script <script>, where the script prints recognized text to stdout
  4. Sends the recognized UTF-8 text back to the board by running:
     qiban_voice_service import-asr <recognized_text>

Examples:
  ./tools/audio/qiban_asr_worker.sh \
    --adb-serial 123456F \
    --recognized-text "导航到清华大学"

  ./tools/audio/qiban_asr_worker.sh \
    --adb-serial 123456F \
    --asr-script ./host_asr.sh

Notes:
  - Default board request path:
      /data/qiban_voice/asr/last_request.json
  - Default board callback command:
      qiban_voice_service import-asr
EOF
}

ADB_SERIAL=""
BOARD_REQUEST="/data/qiban_voice/asr/last_request.json"
REQUEST_PATH=""
WORKDIR="/tmp/qiban_asr"
LOCAL_PCM_PATH=""
RECOGNIZED_TEXT=""
RECOGNIZED_TEXT_FILE=""
ASR_SCRIPT=""
IMPORT_COMMAND="qiban_voice_service import-asr"
WAIT_RETRIES=20
WAIT_INTERVAL_S=1

json_get_string() {
  local key="$1"
  local path="$2"
  sed -n "s/.*\"${key}\":[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p" "$path" | head -n 1
}

json_get_int() {
  local key="$1"
  local path="$2"
  sed -n "s/.*\"${key}\":[[:space:]]*\\([-0-9][0-9]*\\).*/\\1/p" "$path" | head -n 1
}

adb_cmd() {
  if [[ -n "$ADB_SERIAL" ]]; then
    adb -s "$ADB_SERIAL" "$@"
  else
    adb "$@"
  fi
}

shell_quote() {
  printf "'%s'" "${1//\'/\'\"\'\"\'}"
}

wait_for_board_pcm() {
  local board_pcm="$1"
  local local_pcm="$2"
  local tmp_pcm="${local_pcm}.tmp"
  local prev_size="-1"
  local size=""
  local i=""

  mkdir -p "$(dirname "$local_pcm")"
  rm -f "$tmp_pcm"

  for i in $(seq 1 "$WAIT_RETRIES"); do
    if adb_cmd pull "$board_pcm" "$tmp_pcm" >/dev/null 2>&1; then
      size="$(wc -c < "$tmp_pcm" | tr -d ' ')"
      if [[ "$size" -gt 0 && "$size" == "$prev_size" ]]; then
        mv -f "$tmp_pcm" "$local_pcm"
        return 0
      fi
      prev_size="$size"
    fi
    sleep "$WAIT_INTERVAL_S"
  done

  rm -f "$tmp_pcm"
  echo "Timed out waiting for board PCM: $board_pcm" >&2
  return 1
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
    --local-pcm)
      LOCAL_PCM_PATH="${2:-}"
      shift 2
      ;;
    --recognized-text)
      RECOGNIZED_TEXT="${2:-}"
      shift 2
      ;;
    --recognized-text-file)
      RECOGNIZED_TEXT_FILE="${2:-}"
      shift 2
      ;;
    --asr-script)
      ASR_SCRIPT="${2:-}"
      shift 2
      ;;
    --import-command)
      IMPORT_COMMAND="${2:-}"
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

mkdir -p "$WORKDIR"

if [[ -z "$REQUEST_PATH" ]]; then
  if ! command -v adb >/dev/null 2>&1; then
    echo "adb is required when --request is not provided." >&2
    exit 1
  fi

  REQUEST_PATH="${WORKDIR}/last_request.json"
  adb_cmd shell "cat $(shell_quote "$BOARD_REQUEST")" > "$REQUEST_PATH"
fi

if [[ ! -f "$REQUEST_PATH" ]]; then
  echo "ASR request file not found: $REQUEST_PATH" >&2
  exit 1
fi

PCM_PATH="$(json_get_string pcm_path "$REQUEST_PATH")"
DURATION_S="$(json_get_int duration_s "$REQUEST_PATH")"
CALLBACK_COMMAND="$(json_get_string callback_command "$REQUEST_PATH")"

if [[ -z "$PCM_PATH" ]]; then
  echo "pcm_path missing in request: $REQUEST_PATH" >&2
  exit 1
fi

if [[ -z "$CALLBACK_COMMAND" ]]; then
  CALLBACK_COMMAND="$IMPORT_COMMAND"
fi

if [[ -z "$LOCAL_PCM_PATH" ]]; then
  if [[ "$REQUEST_PATH" == "${WORKDIR}/last_request.json" ]]; then
    LOCAL_PCM_PATH="${WORKDIR}/$(basename "$PCM_PATH")"
    wait_for_board_pcm "$PCM_PATH" "$LOCAL_PCM_PATH"
  else
    LOCAL_PCM_PATH="$PCM_PATH"
  fi
fi

if [[ ! -f "$LOCAL_PCM_PATH" ]]; then
  echo "Local PCM file not found: $LOCAL_PCM_PATH" >&2
  exit 1
fi

if [[ -n "$RECOGNIZED_TEXT_FILE" ]]; then
  if [[ ! -f "$RECOGNIZED_TEXT_FILE" ]]; then
    echo "Recognized text file not found: $RECOGNIZED_TEXT_FILE" >&2
    exit 1
  fi
  RECOGNIZED_TEXT="$(tr '\n' ' ' < "$RECOGNIZED_TEXT_FILE" | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')"
elif [[ -n "$ASR_SCRIPT" ]]; then
  if [[ ! -x "$ASR_SCRIPT" ]]; then
    echo "ASR script is not executable: $ASR_SCRIPT" >&2
    exit 1
  fi
  RECOGNIZED_TEXT="$("$ASR_SCRIPT" "$LOCAL_PCM_PATH" | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')"
fi

if [[ -z "$RECOGNIZED_TEXT" ]]; then
  echo "No recognized text was provided. Use --recognized-text, --recognized-text-file, or --asr-script." >&2
  exit 1
fi

printf '%s\n' "$RECOGNIZED_TEXT" > "${WORKDIR}/recognized_text.txt"

echo "ASR request: ${REQUEST_PATH}"
echo "PCM: ${LOCAL_PCM_PATH}"
if [[ -n "$DURATION_S" ]]; then
  echo "Expected duration: ${DURATION_S}s"
fi
echo "Recognized text: ${RECOGNIZED_TEXT}"

if command -v adb >/dev/null 2>&1 && [[ "$REQUEST_PATH" == "${WORKDIR}/last_request.json" ]]; then
  adb_cmd shell "${CALLBACK_COMMAND} $(shell_quote "$RECOGNIZED_TEXT")"
  echo "Returned recognized text to board."
else
  echo "Board callback not executed automatically."
  echo "Run on board:"
  echo "  ${CALLBACK_COMMAND} $(shell_quote "$RECOGNIZED_TEXT")"
fi
