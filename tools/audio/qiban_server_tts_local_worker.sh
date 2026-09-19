#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  qiban_server_tts_local_worker.sh --server-url <url> [--worker-id <id>]
                                   [--workdir <dir>] [--tts-script <script>]

What it does:
  1. Claims one pending TTS job from the relay server.
  2. Downloads the request text file.
  3. Runs the local TTS script to produce 16kHz/16-bit/mono PCM.
  4. Uploads the PCM to /api/jobs/<id>/result.
EOF
}

SERVER_URL=""
WORKER_ID="host-tts-$(hostname)"
WORKDIR="/tmp/qiban_server_tts"
TTS_SCRIPT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --server-url)
      SERVER_URL="${2:-}"
      shift 2
      ;;
    --worker-id)
      WORKER_ID="${2:-}"
      shift 2
      ;;
    --workdir)
      WORKDIR="${2:-}"
      shift 2
      ;;
    --tts-script)
      TTS_SCRIPT="${2:-}"
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

if [[ -z "${SERVER_URL}" ]]; then
  echo "--server-url is required." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "${TTS_SCRIPT}" ]]; then
  TTS_SCRIPT="${SCRIPT_DIR}/qiban_tts_local.sh"
fi

if [[ ! -x "${TTS_SCRIPT}" ]]; then
  echo "TTS script is not executable: ${TTS_SCRIPT}" >&2
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required." >&2
  exit 1
fi

mkdir -p "${WORKDIR}"

PENDING_JSON="$(curl -fsS "${SERVER_URL%/}/api/jobs/pending?job_type=tts&limit=1&claim=true&worker_id=${WORKER_ID}")"
JOB_ID="$(printf '%s' "${PENDING_JSON}" | python3 -c 'import json,sys; data=json.load(sys.stdin); items=data.get("items", []); print(items[0]["id"] if items else "")')"

if [[ -z "${JOB_ID}" ]]; then
  echo "No pending TTS jobs."
  exit 0
fi

REQUEST_PATH="${WORKDIR}/${JOB_ID}.txt"
OUTPUT_PCM="${WORKDIR}/${JOB_ID}.pcm"
curl -fsS "${SERVER_URL%/}/api/jobs/${JOB_ID}/request-file" -o "${REQUEST_PATH}"

if ! "${TTS_SCRIPT}" --request "${REQUEST_PATH}" --output "${OUTPUT_PCM}" >/dev/null; then
  ERROR_JSON="$(python3 -c 'import json; print(json.dumps({"stage":"tts","status":"script_failed"}, ensure_ascii=False))')"
  curl -fsS -X POST "${SERVER_URL%/}/api/jobs/${JOB_ID}/result" \
    -F status=failed \
    -F "result_payload=${ERROR_JSON}" \
    -F "error_message=local tts script failed" >/dev/null
  exit 1
fi

RESULT_JSON="$(python3 -c 'import json; print(json.dumps({"sample_rate_hz":16000,"channels":1,"bits_per_sample":16,"format":"pcm_s16le_mono"}, ensure_ascii=False))')"
curl -fsS -X POST "${SERVER_URL%/}/api/jobs/${JOB_ID}/result" \
  -F status=done \
  -F "result_payload=${RESULT_JSON}" \
  -F "result_file=@${OUTPUT_PCM};type=application/octet-stream" >/dev/null

echo "Completed TTS job ${JOB_ID}: ${OUTPUT_PCM}"
