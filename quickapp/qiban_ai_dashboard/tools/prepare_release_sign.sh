#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SIGN_DIR="$ROOT_DIR/sign/release"
PRIVATE_KEY="$SIGN_DIR/private.pem"
CERTIFICATE="$SIGN_DIR/certificate.pem"

mkdir -p "$SIGN_DIR"

if [[ -s "$PRIVATE_KEY" && -s "$CERTIFICATE" ]]; then
  echo "release signing files already exist:"
  echo "  $PRIVATE_KEY"
  echo "  $CERTIFICATE"
  exit 0
fi

openssl req \
  -x509 \
  -newkey rsa:2048 \
  -sha256 \
  -nodes \
  -days 3650 \
  -subj "/CN=Qiban AI Dashboard/O=contest2026_110_hahaha/C=CN" \
  -keyout "$PRIVATE_KEY" \
  -out "$CERTIFICATE"

chmod 600 "$PRIVATE_KEY"
chmod 644 "$CERTIFICATE"

echo "created release signing files:"
echo "  $PRIVATE_KEY"
echo "  $CERTIFICATE"
echo "These files are ignored by git. Keep them local or replace them with the team's official release certificate."
