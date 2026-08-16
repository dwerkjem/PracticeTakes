#!/usr/bin/env bash
# Synchronize this project's secrets, asking for your SSH passphrase once.
#
# The SOPS recipient is derived from ~/.ssh/id_ed25519, and that key carries a
# passphrase, so SOPS cannot reach the identity on its own. SOPS_AGE_KEY_CMD
# does not solve it either: `ssh-to-age -stdinpass` wants the passphrase on
# standard input, and SOPS runs the command with no terminal attached, so it
# reads nothing, fails, and reports only "exit status 1".
#
# So the two jobs are separated. This script derives the identity once,
# interactively, into a file on tmpfs, points SOPS at that file, and shreds it
# on the way out -- including on failure and on Ctrl-C.
#
#   sync-secrets.sh                     synchronize both directions (default)
#   sync-secrets.sh encrypt             local plaintext -> encrypted mirrors
#   sync-secrets.sh decrypt             encrypted mirrors -> local plaintext
#   sync-secrets.sh sync --prefer local resolve conflicts toward the working tree
#
# Anything after the subcommand is passed through to secrets_manager.py.

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "${script_dir}/../../.." && pwd)
manager=${script_dir}/secrets_manager.py
sops_config=${repo_dir}/.sops.yaml
ssh_key=${SSH_KEY:-${HOME}/.ssh/id_ed25519}

command=${1:-sync}
if [[ ${command} == --help || ${command} == -h ]]; then
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
fi
shift || true

identity_file=

cleanup() {
    # The derived identity decrypts every secret this project has. It must not
    # outlive the run, whatever ended it.
    if [[ -n ${identity_file} && -f ${identity_file} ]]; then
        shred -u "${identity_file}" 2>/dev/null || rm -f "${identity_file}"
    fi
}
trap cleanup EXIT INT TERM

fail() {
    printf 'sync-secrets: %s\n' "$1" >&2
    exit 1
}

for required in ssh-to-age python3 shred; do
    command -v "${required}" >/dev/null 2>&1 ||
        fail "${required} is required. See docs/development/operations/SECRETS.md."
done
[[ -f ${manager} ]] || fail "secrets_manager.py not found at ${manager}."
[[ -f ${ssh_key} ]] || fail "no SSH key at ${ssh_key}. Set SSH_KEY to override."

# Refuse before asking for a passphrase if the key cannot be the right one.
# A wall of SOPS recipient errors after the fact teaches nobody anything.
if [[ -f ${ssh_key}.pub && -f ${sops_config} ]]; then
    derived=$(ssh-to-age -i "${ssh_key}.pub" 2>/dev/null || true)
    if [[ -n ${derived} ]] && ! grep -qF "${derived}" "${sops_config}"; then
        printf 'sync-secrets: %s does not decrypt this repository.\n' "${ssh_key}" >&2
        printf '  derives to:  %s\n' "${derived}" >&2
        printf '  .sops.yaml wants a recipient this key does not produce.\n' >&2
        exit 1
    fi
fi

# tmpfs, so the identity is never written to a disk that could be recovered.
scratch_dir=/dev/shm
[[ -d ${scratch_dir} && -w ${scratch_dir} ]] || scratch_dir=${XDG_RUNTIME_DIR:-}
[[ -n ${scratch_dir} && -d ${scratch_dir} ]] ||
    fail "no writable tmpfs (/dev/shm or XDG_RUNTIME_DIR) to hold the identity."

umask 077
identity_file=$(mktemp "${scratch_dir}/practice-takes-age.XXXXXXXX")

# read -rs keeps the passphrase off the terminal and out of shell history;
# printf is a builtin, so it never appears in any process's argument list.
IFS= read -rsp "Passphrase for ${ssh_key}: " passphrase
printf '\n'
if ! printf '%s' "${passphrase}" |
        ssh-to-age -private-key -stdinpass -i "${ssh_key}" > "${identity_file}" 2>/dev/null; then
    passphrase=
    fail "ssh-to-age could not read ${ssh_key}. Wrong passphrase, or the key is not ed25519."
fi
passphrase=
[[ -s ${identity_file} ]] || fail "ssh-to-age produced an empty identity."

# SOPS tries every source it knows. Leaving a stale KEY or KEY_CMD set turns a
# clean failure here into an ambiguous one.
unset SOPS_AGE_KEY SOPS_AGE_KEY_CMD SOPS_AGE_SSH_PRIVATE_KEY_FILE SOPS_AGE_SSH_PRIVATE_KEY_CMD
export SOPS_AGE_KEY_FILE=${identity_file}

python3 "${manager}" "${command}" "$@"
