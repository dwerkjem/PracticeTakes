#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
SERVICE_DIR="$PROJECT_ROOT/services/feedback-intake"
MODE=local
USE_WRANGLER_LOGIN=false

usage() {
    cat <<'EOF'
Usage: ./scripts/feedback/migrate-feedback-database.sh [--local | --remote] [--wrangler-login]

Apply pending feedback-service D1 migrations.

  --local             Migrate Wrangler's local development database (default).
  --remote            Migrate the configured hosted Cloudflare D1 database.
  --wrangler-login    Ignore CLOUDFLARE_API_TOKEN and use Wrangler OAuth login.
  --help              Show this help text.
EOF
}

while (( $# > 0 )); do
    case "$1" in
        --local)
            MODE=local
            ;;
        --remote)
            MODE=remote
            ;;
        --wrangler-login)
            USE_WRANGLER_LOGIN=true
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            printf 'Error: unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ ! -f "$SERVICE_DIR/wrangler.jsonc" ]]; then
    printf 'Error: missing %s/wrangler.jsonc\n' "$SERVICE_DIR" >&2
    exit 1
fi

wrangler="$SERVICE_DIR/node_modules/wrangler/bin/wrangler.js"
if [[ ! -f "$wrangler" ]]; then
    printf 'Error: feedback service dependencies are not installed.\n' >&2
    printf 'Run: cd services && npm install\n' >&2
    exit 1
fi

node_binary="$(command -v node || true)"
if [[ -z "$node_binary" ]]; then
    printf 'Error: Node.js is required to run Wrangler.\n' >&2
    exit 1
fi

printf 'Applying %s feedback database migrations...\n' "$MODE"
command=(
    "$node_binary"
    "$wrangler"
    d1 migrations apply
    practice-takes-feedback
    "--$MODE"
)

cd "$SERVICE_DIR"
if [[ "$USE_WRANGLER_LOGIN" == true ]]; then
    CLOUDFLARE_API_TOKEN= "${command[@]}"
else
    "${command[@]}"
fi
