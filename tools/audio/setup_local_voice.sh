#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "${SCRIPT_DIR}/qiban_voice_env.sh"

usage() {
  cat <<'EOF'
Usage:
  setup_local_voice.sh [--asr-only | --tts-only]

What it does:
  1. Bootstraps a repo-local Python package directory under tools/audio/.venv_voice/
  2. Installs the Python runtime dependencies for local ASR/TTS
  3. Downloads the default ASR model into tools/audio/models/

Environment overrides:
  QIBAN_ASR_MODEL_URL
  QIBAN_ASR_MODEL_DIR
  QIBAN_TTS_VOICE
EOF
}

INSTALL_ASR=1
INSTALL_TTS=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --asr-only)
      INSTALL_TTS=0
      shift
      ;;
    --tts-only)
      INSTALL_ASR=0
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

mkdir -p "${QIBAN_AUDIO_CACHE_DIR}" "${QIBAN_AUDIO_MODELS_DIR}"

if [[ ! -d "${QIBAN_AUDIO_PYDEPS_DIR}/pip" ]]; then
  echo "Missing pip bootstrap directory: ${QIBAN_AUDIO_PYDEPS_DIR}" >&2
  exit 1
fi

echo "Installing Python runtime packages into ${QIBAN_AUDIO_SITE_DIR}"
rm -rf "${QIBAN_AUDIO_SITE_DIR}"
mkdir -p "${QIBAN_AUDIO_SITE_DIR}"
PYTHONPATH="${QIBAN_AUDIO_PYDEPS_DIR}" python3 -m pip install \
  --disable-pip-version-check \
  --upgrade \
  --target "${QIBAN_AUDIO_SITE_DIR}" \
  -r "${SCRIPT_DIR}/requirements-voice.txt"

PYTHONPATH="${QIBAN_AUDIO_PYDEPS_DIR}" python3 -m pip install \
  --disable-pip-version-check \
  --upgrade \
  --target "${QIBAN_AUDIO_SITE_DIR}" \
  --no-deps \
  vosk

download_file() {
  local url="$1"
  local dst="$2"

  python3 - "$url" "$dst" <<'PY'
import pathlib
import sys
import urllib.request

url = sys.argv[1]
dst = pathlib.Path(sys.argv[2])
dst.parent.mkdir(parents=True, exist_ok=True)
tmp = dst.with_suffix(dst.suffix + ".tmp")

if dst.exists() and dst.stat().st_size > 0:
    raise SystemExit(0)

resume_from = tmp.stat().st_size if tmp.exists() else 0
request = urllib.request.Request(url)
if resume_from > 0:
    request.add_header("Range", f"bytes={resume_from}-")

with urllib.request.urlopen(request) as resp:
    append_mode = resume_from > 0 and getattr(resp, "status", None) == 206
    mode = "ab" if append_mode else "wb"
    if mode == "wb":
        resume_from = 0

    with tmp.open(mode) as fp:
        while True:
            chunk = resp.read(1024 * 1024)
            if not chunk:
                break
            fp.write(chunk)

if tmp.stat().st_size == 0:
    raise SystemExit("download produced an empty file")

tmp.replace(dst)
PY
}

extract_zip() {
  local archive="$1"
  local dst_dir="$2"

  python3 - "$archive" "$dst_dir" <<'PY'
import pathlib
import shutil
import sys
import zipfile

archive = pathlib.Path(sys.argv[1])
dst_dir = pathlib.Path(sys.argv[2])
tmp_dir = dst_dir.parent / (dst_dir.name + ".tmp")

if tmp_dir.exists():
    shutil.rmtree(tmp_dir)
tmp_dir.mkdir(parents=True, exist_ok=True)

with zipfile.ZipFile(archive) as zf:
    zf.extractall(tmp_dir)

entries = [p for p in tmp_dir.iterdir()]
if len(entries) == 1 and entries[0].is_dir():
    extracted_root = entries[0]
else:
    extracted_root = tmp_dir

if dst_dir.exists():
    shutil.rmtree(dst_dir)

if extracted_root != tmp_dir:
    extracted_root.replace(dst_dir)
    shutil.rmtree(tmp_dir, ignore_errors=True)
else:
    tmp_dir.replace(dst_dir)
PY
}

if [[ "${INSTALL_ASR}" -eq 1 ]]; then
  ASR_ARCHIVE="${QIBAN_AUDIO_CACHE_DIR}/$(basename "${QIBAN_ASR_MODEL_URL%%\?*}")"
  echo "Downloading ASR model from ${QIBAN_ASR_MODEL_URL}"
  download_file "${QIBAN_ASR_MODEL_URL}" "${ASR_ARCHIVE}"
  echo "Extracting ASR model to ${QIBAN_ASR_MODEL_DIR}"
  extract_zip "${ASR_ARCHIVE}" "${QIBAN_ASR_MODEL_DIR}"
fi

if [[ "${INSTALL_TTS}" -eq 1 ]]; then
  echo "Using Edge TTS voice: ${QIBAN_TTS_VOICE}"
fi

cat <<EOF
Local voice runtime is prepared.
ASR model dir: ${QIBAN_ASR_MODEL_DIR}
TTS voice: ${QIBAN_TTS_VOICE}
EOF
