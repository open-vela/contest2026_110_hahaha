#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${SCRIPT_DIR}/.env"

usage() {
  cat <<'EOF'
Usage:
  deploy.sh [up|down|logs|ps]

Notes:
  1. Copy .env.example to .env and fill in QIBAN_SERVER_DOMAIN / QIBAN_ACME_EMAIL first.
  2. Ensure this host has a public IP and ports 80/443 are open.
  3. Ensure your domain A record points to this host before running "up".
EOF
}

if [[ ! -f "${ENV_FILE}" ]]; then
  echo ".env not found. Copy .env.example to .env first." >&2
  exit 1
fi

ACTION="${1:-up}"
case "${ACTION}" in
  up)
    docker compose --env-file "${ENV_FILE}" -f "${SCRIPT_DIR}/docker-compose.yml" up -d --build
    ;;
  down)
    docker compose --env-file "${ENV_FILE}" -f "${SCRIPT_DIR}/docker-compose.yml" down
    ;;
  logs)
    docker compose --env-file "${ENV_FILE}" -f "${SCRIPT_DIR}/docker-compose.yml" logs -f --tail=200
    ;;
  ps)
    docker compose --env-file "${ENV_FILE}" -f "${SCRIPT_DIR}/docker-compose.yml" ps
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    echo "Unknown action: ${ACTION}" >&2
    usage >&2
    exit 1
    ;;
esac
