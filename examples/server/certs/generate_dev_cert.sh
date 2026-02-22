#!/usr/bin/env bash
set -euo pipefail

# Generates a self-signed cert and key in the current directory
OUT_CERT=dev.crt
OUT_KEY=dev.key

openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout "$OUT_KEY" -out "$OUT_CERT" -subj "/CN=localhost"

echo "Generated $OUT_CERT and $OUT_KEY"
