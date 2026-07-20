#!/bin/sh
set -eu

: "${CLOUDFLARE_ACCOUNT_ID:?CLOUDFLARE_ACCOUNT_ID must be set}"
: "${CLOUDFLARE_API_TOKEN:?CLOUDFLARE_API_TOKEN must be set}"
: "${D1_DATABASE_ID:?D1_DATABASE_ID must be set}"
: "${ADMIN_EMAIL:?ADMIN_EMAIL must be set}"
: "${ADMIN_PASSWORD:?ADMIN_PASSWORD must be set}"

if [ "${#ADMIN_PASSWORD}" -lt 16 ]; then
  echo "ADMIN_PASSWORD must contain at least 16 characters" >&2
  exit 1
fi

exec node dist/docker-server.js
