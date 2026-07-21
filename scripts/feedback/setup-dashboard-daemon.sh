#!/usr/bin/env bash
set -euo pipefail

service_name=practice-takes-feedback-dashboard
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "${script_dir}/../.." && pwd)
dashboard_dir=${repo_dir}/services/feedback-intake
compose_file=${dashboard_dir}/compose.yaml
environment_file=${dashboard_dir}/.env
unit_file=/etc/systemd/system/${service_name}.service

if [[ $# -gt 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi

if ! command -v docker >/dev/null 2>&1 || ! docker compose version >/dev/null 2>&1; then
    echo "Docker with the Compose plugin is required." >&2
    exit 1
fi

if ! command -v systemctl >/dev/null 2>&1; then
    echo "systemd is required." >&2
    exit 1
fi

if [[ ! -f ${environment_file} ]]; then
    : "${CLOUDFLARE_ACCOUNT_ID:?Set CLOUDFLARE_ACCOUNT_ID before running setup}"
    : "${CLOUDFLARE_API_TOKEN:?Set CLOUDFLARE_API_TOKEN before running setup}"
    : "${D1_DATABASE_ID:?Set D1_DATABASE_ID before running setup}"
    : "${ADMIN_EMAIL:?Set ADMIN_EMAIL before running setup}"
    admin_password=${ADMIN_PASSWORD:-$(openssl rand -base64 32 | tr -d '\n')}
    for value in "${CLOUDFLARE_ACCOUNT_ID}" "${CLOUDFLARE_API_TOKEN}" "${D1_DATABASE_ID}" \
                 "${ADMIN_EMAIL}" "${admin_password}"; do
        if [[ ${value} == *$'\n'* ]]; then
            echo "Configuration values must be a single line." >&2
            exit 2
        fi
    done
    umask 077
    {
        printf 'CLOUDFLARE_ACCOUNT_ID=%s\n' "${CLOUDFLARE_ACCOUNT_ID}"
        printf 'CLOUDFLARE_API_TOKEN=%s\n' "${CLOUDFLARE_API_TOKEN}"
        printf 'D1_DATABASE_ID=%s\n' "${D1_DATABASE_ID}"
        printf 'ADMIN_EMAIL=%s\n' "${ADMIN_EMAIL}"
        printf 'ADMIN_PASSWORD=%s\n' "${admin_password}"
        printf 'FEEDBACK_PORT=8787\n'
    } > "${environment_file}"
    echo "Created ${environment_file}."
    echo "Dashboard password: ${admin_password}"
    echo "Store this password now; it will not be printed again."
else
    echo "Using existing ${environment_file}."
fi

docker compose --env-file "${environment_file}" -f "${compose_file}" build

temporary_unit=$(mktemp)
trap 'rm -f -- "${temporary_unit}"' EXIT
cat > "${temporary_unit}" <<EOF
[Unit]
Description=Practice Takes feedback dashboard
Requires=docker.service
After=docker.service network-online.target
Wants=network-online.target

[Service]
Type=oneshot
RemainAfterExit=yes
WorkingDirectory=${dashboard_dir}
ExecStart=$(command -v docker) compose --env-file ${environment_file} -f ${compose_file} up --detach --remove-orphans
ExecStop=$(command -v docker) compose --env-file ${environment_file} -f ${compose_file} down
TimeoutStartSec=0
TimeoutStopSec=60

[Install]
WantedBy=multi-user.target
EOF

sudo install -o root -g root -m 0644 "${temporary_unit}" "${unit_file}"
sudo systemctl daemon-reload
sudo systemctl enable --now "${service_name}.service"

echo "The dashboard daemon is running at http://127.0.0.1:8787/admin"
echo "It connects to the configured cloud-hosted D1 database through the Cloudflare API."
