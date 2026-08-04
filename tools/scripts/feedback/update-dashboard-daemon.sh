#!/usr/bin/env bash
set -euo pipefail

service_name=practice-takes-feedback-dashboard
compose_project=feedback-intake
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "${script_dir}/../../.." && pwd)
dashboard_dir=${repo_dir}/src/services/feedback-intake
compose_file=${dashboard_dir}/compose.yaml
environment_file=${dashboard_dir}/.env
unit_file=/etc/systemd/system/${service_name}.service
runtime_dir=/etc/practice-takes-feedback
runtime_compose_file=${runtime_dir}/compose.yaml
runtime_environment_file=${runtime_dir}/.env

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

docker compose --project-name "${compose_project}" --env-file "${environment_file}" \
    -f "${compose_file}" build --pull

sudo install -o root -g root -m 0755 -d "${runtime_dir}"
sudo install -o root -g root -m 0644 "${compose_file}" "${runtime_compose_file}"
sudo install -o root -g root -m 0600 "${environment_file}" "${runtime_environment_file}"

if [[ -r ${unit_file} ]] && ! grep -qF -- "${runtime_compose_file}" "${unit_file}"; then
    echo "Warning: ${unit_file} still runs from ${dashboard_dir}." >&2
    echo "Re-run setup-dashboard-daemon.sh to move the unit onto ${runtime_dir}." >&2
fi

if ! sudo systemctl restart "${service_name}.service"; then
    echo "The service did not start. Read the reason with:" >&2
    echo "  sudo journalctl -u ${service_name}.service -n 50" >&2
    exit 1
fi
docker compose --project-name "${compose_project}" --env-file "${environment_file}" \
    -f "${compose_file}" ps

echo "The feedback dashboard daemon has been updated."
