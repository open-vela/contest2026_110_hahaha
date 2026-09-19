#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  qiban_tts_worker.sh --request <last_request.txt> --source <tts_audio> --output <last_tts_16k_s16_mono.pcm>

What it does:
  1. Reads the board-side TTS request text.
  2. Accepts an audio file already synthesized by any real TTS engine.
  3. Normalizes that audio to raw PCM: 16 kHz / 16-bit / mono / signed little-endian.

Notes:
  - This script is the host/cloud-side handoff point.
  - The actual synthesis engine is intentionally decoupled. It can be Edge TTS,
    Azure, a private model service, iFlytek, Volcano, CosyVoice, etc.
  - If the source file already ends with .pcm, this script copies it directly.
EOF
}

REQUEST_PATH=""
SOURCE_PATH=""
OUTPUT_PATH=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --request)
      REQUEST_PATH="${2:-}"
      shift 2
      ;;
    --source)
      SOURCE_PATH="${2:-}"
      shift 2
      ;;
    --output)
      OUTPUT_PATH="${2:-}"
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

if [[ -z "$REQUEST_PATH" || -z "$SOURCE_PATH" || -z "$OUTPUT_PATH" ]]; then
  usage >&2
  exit 1
fi

if [[ ! -f "$REQUEST_PATH" ]]; then
  echo "Request text file not found: $REQUEST_PATH" >&2
  exit 1
fi

if [[ ! -f "$SOURCE_PATH" ]]; then
  echo "Synthesized audio file not found: $SOURCE_PATH" >&2
  exit 1
fi

REQUEST_TEXT="$(tr '\n' ' ' < "$REQUEST_PATH" | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')"
mkdir -p "$(dirname "$OUTPUT_PATH")"

echo "TTS request: ${REQUEST_TEXT}"
echo "Source audio: ${SOURCE_PATH}"
echo "Board PCM output: ${OUTPUT_PATH}"

if [[ "$SOURCE_PATH" == *.pcm ]]; then
  cp -f "$SOURCE_PATH" "$OUTPUT_PATH"
  echo "Copied PCM without transcoding."
  exit 0
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg is required to normalize non-PCM TTS outputs." >&2
  exit 1
fi

ffmpeg -y -i "$SOURCE_PATH" \
  -f s16le \
  -acodec pcm_s16le \
  -ac 1 \
  -ar 16000 \
  "$OUTPUT_PATH" >/dev/null 2>&1

echo "Normalized synthesized audio to 16k/16bit/mono PCM."
