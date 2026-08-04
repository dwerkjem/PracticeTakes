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

if [[ $# -gt 0 ]]; then
    echo "Usage: $0" >&2
    exit 2
fi

is_root_owned() {
    local target=$1 owner mode
    owner=$(stat -c '%u' -- "${target}" 2>/dev/null) || return 1
    mode=$(stat -c '%a' -- "${target}" 2>/dev/null) || return 1
    [[ ${owner} == 0 ]] || return 1
    (( (8#${mode} & 0022) == 0 ))
}

trusted_binary_path() {
    local candidate=$1 resolved directory
    [[ -n ${candidate} && -x ${candidate} ]] || return 1
    resolved=$(readlink -f -- "${candidate}" 2>/dev/null) || return 1
    [[ -x ${resolved} ]] || return 1
    is_root_owned "${resolved}" || return 1
    directory=${resolved%/*}
    while [[ -n ${directory} ]]; do
        is_root_owned "${directory}" || return 1
        directory=${directory%/*}
    done
    is_root_owned / || return 1
    printf '%s\n' "${resolved}"
}

require_unit_safe() {
    local label=$1 value=$2
    if [[ -z ${value} || ${value} == *[!A-Za-z0-9@:/._-]* ]]; then
        echo "Refusing to write ${unit_file}: ${label} is empty or has unsupported characters." >&2
        exit 1
    fi
}

if ! command -v docker >/dev/null 2>&1 || ! docker compose version >/dev/null 2>&1; then
    echo "Docker with the Compose plugin is required." >&2
    exit 1
fi

if ! command -v systemctl >/dev/null 2>&1; then
    echo "systemd is required." >&2
    exit 1
fi

path_docker=$(readlink -f -- "$(command -v docker)")
docker_binary=
for candidate in /usr/bin/docker /usr/local/bin/docker /bin/docker "${path_docker}"; do
    if docker_binary=$(trusted_binary_path "${candidate}"); then
        break
    fi
    docker_binary=
done

if [[ -z ${docker_binary} ]]; then
    echo "No root-owned docker binary was found in /usr/bin, /usr/local/bin, /bin, or on PATH." >&2
    echo "${unit_file} runs as root, so it must not call a binary that an unprivileged user can replace." >&2
    exit 1
fi

require_unit_safe "the docker path" "${docker_binary}"

if [[ ${docker_binary} != "${path_docker}" ]]; then
    echo "Note: docker on PATH is ${path_docker}; the unit will call ${docker_binary}." >&2
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
    echo "Read the dashboard password from ADMIN_PASSWORD in that file."
else
    echo "Using existing ${environment_file}."
fi
chmod 600 "${environment_file}"

docker compose --project-name "${compose_project}" --env-file "${environment_file}" \
    -f "${compose_file}" build

image_preflight=
while IFS= read -r image; do
    [[ -n ${image} ]] || continue
    require_unit_safe "the image name ${image}" "${image}"
    image_preflight+="ExecStartPre=/bin/sh -c '${docker_binary} image inspect ${image} >/dev/null 2>&1 || { echo \"Image ${image} is not present. Run tools/scripts/feedback/update-dashboard-daemon.sh from the Practice Takes checkout to rebuild it.\" >&2; exit 1; }'"$'\n'
done < <(docker compose --project-name "${compose_project}" --env-file "${environment_file}" \
    -f "${compose_file}" config --images)

if [[ -z ${image_preflight} ]]; then
    echo "Could not read the image names from ${compose_file}." >&2
    exit 1
fi
image_preflight=${image_preflight%$'\n'}

sudo install -o root -g root -m 0755 -d "${runtime_dir}"
sudo install -o root -g root -m 0644 "${compose_file}" "${runtime_compose_file}"
sudo install -o root -g root -m 0600 "${environment_file}" "${runtime_environment_file}"

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
WorkingDirectory=${runtime_dir}
# Images come from tools/scripts/feedback/update-dashboard-daemon.sh, which builds
# them in the checkout. This unit only starts images that already exist, so
# check for them first and say what to run when one has been pruned.
${image_preflight}
ExecStart=${docker_binary} compose --project-name ${compose_project} --env-file ${runtime_environment_file} -f ${runtime_compose_file} up --detach --no-build --remove-orphans
ExecStop=${docker_binary} compose --project-name ${compose_project} --env-file ${runtime_environment_file} -f ${runtime_compose_file} down
TimeoutStartSec=0
TimeoutStopSec=60

[Install]
WantedBy=multi-user.target
EOF

sudo install -o root -g root -m 0644 "${temporary_unit}" "${unit_file}"
sudo systemctl daemon-reload
sudo systemctl enable "${service_name}.service"
if ! sudo systemctl restart "${service_name}.service"; then
    echo "The service did not start. Read the reason with:" >&2
    echo "  sudo journalctl -u ${service_name}.service -n 50" >&2
    exit 1
fi

echo "The dashboard daemon is running at http://127.0.0.1:8787/admin"
echo "It connects to the configured cloud-hosted D1 database through the Cloudflare API."
