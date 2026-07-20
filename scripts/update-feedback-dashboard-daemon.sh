#!/usr/bin/env bash
set -euo pipefail

service_name=practice-takes-feedback-dashboard
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "${script_dir}/.." && pwd)
dashboard_dir=${repo_dir}/services/feedback-intake
compose_file=${dashboard_dir}/compose.yaml
environment_file=${dashboard_dir}/.env

if [[ ! -f ${environment_file} ]]; then
    echo "Missing ${environment_file}; run setup-feedback-dashboard-daemon.sh first." >&2
    exit 1
fi

if [[ ${1:-} == "--pull-source" ]]; then
    git -C "${repo_dir}" pull --ff-only
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--pull-source]" >&2
    exit 2
fi

docker compose --env-file "${environment_file}" -f "${compose_file}" build --pull
sudo systemctl restart "${service_name}.service"
docker compose --env-file "${environment_file}" -f "${compose_file}" ps

echo "The feedback dashboard daemon has been updated."
