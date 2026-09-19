#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  qiban_server_asr_local_worker.sh --server-url <url> [--worker-id <id>]
                                   [--workdir <dir>] [--asr-script <script>]

What it does:
  1. Claims one pending ASR job from the relay server.
  2. Downloads the uploaded PCM request file.
  3. Runs the local ASR script.
  4. Posts the recognized text back to /api/jobs/<id>/result.
EOF
}

SERVER_URL=""
WORKER_ID="host-asr-$(hostname)"
WORKDIR="/tmp/qiban_server_asr"
ASR_SCRIPT=""

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
    --asr-script)
      ASR_SCRIPT="${2:-}"
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
if [[ -z "${ASR_SCRIPT}" ]]; then
  ASR_SCRIPT="${SCRIPT_DIR}/qiban_asr_local.sh"
fi

if [[ ! -x "${ASR_SCRIPT}" ]]; then
  echo "ASR script is not executable: ${ASR_SCRIPT}" >&2
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required." >&2
  exit 1
fi

mkdir -p "${WORKDIR}"

PENDING_JSON="$(curl -fsS "${SERVER_URL%/}/api/jobs/pending?job_type=asr&limit=1&claim=true&worker_id=${WORKER_ID}")"
JOB_ID="$(printf '%s' "${PENDING_JSON}" | python3 -c 'import json,sys; data=json.load(sys.stdin); items=data.get("items", []); print(items[0]["id"] if items else "")')"

if [[ -z "${JOB_ID}" ]]; then
  echo "No pending ASR jobs."
  exit 0
fi

PCM_PATH="${WORKDIR}/${JOB_ID}.pcm"
curl -fsS "${SERVER_URL%/}/api/jobs/${JOB_ID}/request-file" -o "${PCM_PATH}"

if ! RECOGNIZED_TEXT="$("${ASR_SCRIPT}" "${PCM_PATH}" | tr '\n' ' ' | sed 's/[[:space:]]\+/ /g; s/^ //; s/ $//')"; then
  ERROR_JSON="$(python3 -c 'import json; print(json.dumps({"stage":"asr","status":"script_failed"}, ensure_ascii=False))')"
  curl -fsS -X POST "${SERVER_URL%/}/api/jobs/${JOB_ID}/result" \
    -F status=failed \
    -F "result_payload=${ERROR_JSON}" \
    -F "error_message=local asr script failed" >/dev/null
  exit 1
fi

if [[ -z "${RECOGNIZED_TEXT}" ]]; then
  ERROR_JSON="$(python3 -c 'import json; print(json.dumps({"stage":"asr","status":"empty_result"}, ensure_ascii=False))')"
  curl -fsS -X POST "${SERVER_URL%/}/api/jobs/${JOB_ID}/result" \
    -F status=failed \
    -F "result_payload=${ERROR_JSON}" \
    -F "error_message=recognized text is empty" >/dev/null
  exit 1
fi

RESULT_JSON="$(python3 -c 'import json,sys; print(json.dumps({"recognized_text": sys.argv[1]}, ensure_ascii=False))' "${RECOGNIZED_TEXT}")"
curl -fsS -X POST "${SERVER_URL%/}/api/jobs/${JOB_ID}/result" \
  -F status=done \
  -F "result_payload=${RESULT_JSON}" >/dev/null

echo "Completed ASR job ${JOB_ID}: ${RECOGNIZED_TEXT}"
