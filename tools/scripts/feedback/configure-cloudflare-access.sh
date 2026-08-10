#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "${script_dir}/../../.." && pwd)
service_dir=${repo_dir}/src/services/feedback-intake
environment_file=${service_dir}/.env
wrangler=${service_dir}/node_modules/wrangler/bin/wrangler.js
application_name="Practice Takes feedback dashboard"
api_base=https://api.cloudflare.com/client/v4

usage() {
    cat <<'EOF'
Usage: ./tools/scripts/feedback/configure-cloudflare-access.sh

Create or update the path-scoped Cloudflare Access application for the hosted
feedback dashboard. The resulting JWT audience, team domain, and single
administrator and notification email settings are stored as encrypted Worker
secrets.

Configuration is loaded from src/services/feedback-intake/.env. Variables already
exported by the caller take precedence over values in the file.

Required values:
  CLOUDFLARE_ACCOUNT_ID  Account containing the Worker and Zero Trust team
  CLOUDFLARE_SETUP_API_TOKEN
                         Short-lived token with Access Apps/Policies Write and
                         Workers Scripts Write for this account. Falls back to
                         CLOUDFLARE_API_TOKEN for compatibility.
  ACCESS_HOSTNAME        Worker hostname without a scheme or path (defaults to
                         the production Practice Takes Worker)
  ADMIN_EMAIL            The only identity allowed to use the dashboard
  FEEDBACK_NOTIFICATION_FROM
                         Sender on a domain enabled in Cloudflare Email Service
EOF
}

if [[ ${1:-} == "--help" ]]; then
    usage
    exit 0
fi
if (( $# > 0 )); then
    usage >&2
    exit 2
fi

if [[ ! -f ${environment_file} ]]; then
    printf 'Error: environment file not found: %s\n' "${environment_file}" >&2
    echo "Copy src/services/feedback-intake/.env.example to .env and configure it." >&2
    exit 1
fi

while IFS= read -r environment_line || [[ -n ${environment_line} ]]; do
    environment_line=${environment_line%$'\r'}
    if [[ -z ${environment_line} || ${environment_line} =~ ^[[:space:]]*# ]]; then
        continue
    fi
    if [[ ${environment_line} == export[[:space:]]* ]]; then
        environment_line=${environment_line#export }
    fi
    if [[ ${environment_line} != *=* ]]; then
        echo "Error: invalid entry in ${environment_file}; expected NAME=VALUE." >&2
        exit 2
    fi
    environment_name=${environment_line%%=*}
    environment_value=${environment_line#*=}
    if [[ ! ${environment_name} =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        echo "Error: invalid variable name in ${environment_file}." >&2
        exit 2
    fi
    if [[ ! -v ${environment_name} ]]; then
        printf -v "${environment_name}" '%s' "${environment_value}"
        export "${environment_name}"
    fi
done < "${environment_file}"

ACCESS_HOSTNAME=${ACCESS_HOSTNAME:-practice-takes-feedback-intake.derekrneilson.workers.dev}
if [[ -n ${CLOUDFLARE_SETUP_API_TOKEN:-} ]]; then
    CLOUDFLARE_API_TOKEN=${CLOUDFLARE_SETUP_API_TOKEN}
    export CLOUDFLARE_API_TOKEN
fi

: "${CLOUDFLARE_ACCOUNT_ID:?Set CLOUDFLARE_ACCOUNT_ID before running setup}"
: "${CLOUDFLARE_API_TOKEN:?Set CLOUDFLARE_SETUP_API_TOKEN before running setup}"
: "${ACCESS_HOSTNAME:?Set ACCESS_HOSTNAME before running setup}"
: "${ADMIN_EMAIL:?Set ADMIN_EMAIL before running setup}"
: "${FEEDBACK_NOTIFICATION_FROM:?Set FEEDBACK_NOTIFICATION_FROM before running setup}"

for command_name in curl jq node; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        printf 'Error: %s is required.\n' "${command_name}" >&2
        exit 1
    fi
done
if [[ ! -f ${wrangler} ]]; then
    echo "Error: feedback service dependencies are not installed." >&2
    echo "Run: cd services && npm install" >&2
    exit 1
fi
if [[ ! ${CLOUDFLARE_ACCOUNT_ID} =~ ^[0-9a-fA-F]{32}$ ]]; then
    echo "Error: CLOUDFLARE_ACCOUNT_ID must be a 32-character hexadecimal ID." >&2
    exit 2
fi
if [[ ! ${ACCESS_HOSTNAME} =~ ^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$ ||
      ${ACCESS_HOSTNAME} != *.* ]]; then
    echo "Error: ACCESS_HOSTNAME must be a hostname without a scheme, port, or path." >&2
    exit 2
fi
if [[ ! ${ADMIN_EMAIL} =~ ^[^[:space:]@]+@[^[:space:]@]+\.[^[:space:]@]+$ ||
      ${ADMIN_EMAIL} == *,* ]]; then
    echo "Error: ADMIN_EMAIL must contain exactly one valid email address." >&2
    exit 2
fi
if [[ ! ${FEEDBACK_NOTIFICATION_FROM} =~ ^[^[:space:]@]+@[^[:space:]@]+\.[^[:space:]@]+$ ||
      ${FEEDBACK_NOTIFICATION_FROM} == *,* ]]; then
    echo "Error: FEEDBACK_NOTIFICATION_FROM must contain one valid email address." >&2
    exit 2
fi

# A sender on one of these can never be onboarded to Cloudflare Email Sending,
# so accepting it deploys a delivery path that provably cannot deliver — which
# is how the template value reached production unnoticed once already.
sender_domain=${FEEDBACK_NOTIFICATION_FROM##*@}
sender_domain=${sender_domain,,}
case ${sender_domain} in
    example.com | example.org | example.net | example.edu | localhost | \
    *.example.com | *.example.org | *.example.net | *.example.edu | \
    *.invalid | *.test | *.local | *.localhost | *.example | *.workers.dev)
        echo "Error: FEEDBACK_NOTIFICATION_FROM uses ${sender_domain}, which cannot" >&2
        echo "send mail. Use an address on a domain onboarded to Cloudflare Email" >&2
        echo "Sending: npx wrangler email sending enable <domain>" >&2
        echo "See docs/development/operations/EMAIL_DISPATCH.md." >&2
        exit 2
        ;;
esac

api_request() {
    local method=$1
    local path=$2
    local body=${3:-}
    local arguments=(
        --silent
        --show-error
        --fail-with-body
        --request "${method}"
        --header @-
        --header "Content-Type: application/json"
        "${api_base}${path}"
    )
    if [[ -n ${body} ]]; then
        arguments+=(--data "${body}")
    fi
    curl "${arguments[@]}" <<<"Authorization: Bearer ${CLOUDFLARE_API_TOKEN}"
}

require_success() {
    local response=$1
    local operation=$2
    if [[ $(jq -r '.success // false' <<<"${response}") != true ]]; then
        printf 'Cloudflare could not %s:\n' "${operation}" >&2
        jq -r '.errors[]? | "  - \(.message)"' <<<"${response}" >&2
        exit 1
    fi
}

account_path=/accounts/${CLOUDFLARE_ACCOUNT_ID}
organization_response=$(api_request GET "${account_path}/access/organizations")
require_success "${organization_response}" "read the Zero Trust organization"
auth_domain=$(jq -r '.result.auth_domain // empty' <<<"${organization_response}")
if [[ -z ${auth_domain} ]]; then
    echo "Error: the Cloudflare account does not have a Zero Trust team domain." >&2
    exit 1
fi
team_domain=https://${auth_domain}

application_payload=$(jq -n \
    --arg name "${application_name}" \
    --arg admin_path "${ACCESS_HOSTNAME}/admin*" \
    --arg audit_asset "${ACCESS_HOSTNAME}/audit.js" \
    --arg admin_api "${ACCESS_HOSTNAME}/v1/admin*" \
    --arg email "${ADMIN_EMAIL,,}" \
    '{
      name: $name,
      type: "self_hosted",
      session_duration: "8h",
      app_launcher_visible: false,
      destinations: [
        {type: "public", uri: $admin_path},
        {type: "public", uri: $audit_asset},
        {type: "public", uri: $admin_api}
      ],
      policies: [
        {
          name: "Allow the feedback administrator",
          decision: "allow",
          precedence: 1,
          include: [{email: {email: $email}}]
        }
      ]
    }')

applications_response=$(api_request GET "${account_path}/access/apps?per_page=100")
require_success "${applications_response}" "list Access applications"
application_ids=$(jq -r \
    --arg name "${application_name}" \
    '.result[] | select(.name == $name and .type == "self_hosted") | .id' \
    <<<"${applications_response}")
application_count=$(wc -w <<<"${application_ids}")
if (( application_count > 1 )); then
    echo "Error: multiple Access applications use the expected dashboard name." >&2
    echo "Remove the duplicate applications in Cloudflare Zero Trust and rerun setup." >&2
    exit 1
fi

if (( application_count == 1 )); then
    application_id=${application_ids}
    application_response=$(api_request \
        PUT "${account_path}/access/apps/${application_id}" "${application_payload}")
    operation="update the Access application"
else
    application_response=$(api_request \
        POST "${account_path}/access/apps" "${application_payload}")
    operation="create the Access application"
fi
require_success "${application_response}" "${operation}"

audience=$(jq -r '.result.aud // empty' <<<"${application_response}")
if [[ -z ${audience} ]]; then
    echo "Error: Cloudflare did not return the Access application audience tag." >&2
    exit 1
fi

put_worker_secret() {
    local name=$1
    local value=$2
    printf '%s' "${value}" |
        (
            cd "${service_dir}"
            node "${wrangler}" secret put "${name}"
        )
}

put_worker_secret ACCESS_TEAM_DOMAIN "${team_domain}"
put_worker_secret ACCESS_AUD "${audience}"
put_worker_secret ADMIN_EMAILS "${ADMIN_EMAIL,,}"
put_worker_secret FEEDBACK_NOTIFICATION_FROM "${FEEDBACK_NOTIFICATION_FROM,,}"
put_worker_secret FEEDBACK_NOTIFICATION_TO "${ADMIN_EMAIL,,}"
put_worker_secret FEEDBACK_DASHBOARD_URL "https://${ACCESS_HOSTNAME}/admin"

cat <<EOF
Cloudflare Access now protects the feedback dashboard for ${ADMIN_EMAIL,,}.

Protected paths:
  https://${ACCESS_HOSTNAME}/admin*
  https://${ACCESS_HOSTNAME}/audit.js
  https://${ACCESS_HOSTNAME}/v1/admin*

The public feedback intake paths remain outside this Access application.
Queued feedback will be sent to ${ADMIN_EMAIL,,} in at most three email batches
per UTC day.
Deploy the current Worker code before opening the dashboard:
  cd services && npm run deploy --workspace @practice-takes/feedback-intake
EOF
