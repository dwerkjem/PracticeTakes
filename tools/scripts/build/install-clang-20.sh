#!/usr/bin/env bash
#
# Clang 20, for running the RealtimeSanitizer check locally.
#
# On its own rather than in the dependency list, because it is the one thing
# here that adds a repository the distribution does not ship: Debian 13 has
# Clang 19, and RealtimeSanitizer arrived in 20. That is a bigger thing to do to
# somebody's machine than installing a package they already have access to, so
# it is asked for separately and never done silently.
#
# Nothing needs this to build or test Practice Takes. It buys one thing: running
# the audio-callback check on this machine instead of waiting for a pull
# request, where it runs either way.
#
# The testing suite finds whatever this installs on its own -- it tries
# clang++-21, clang++-20, then clang++, and uses the first that actually accepts
# -fsanitize=realtime. Nothing has to be configured afterwards, and `clang`
# itself is left pointing wherever the distribution had it.

set -euo pipefail

readonly wanted=20

if command -v "clang++-${wanted}" >/dev/null 2>&1; then
    printf 'clang++-%s is already installed.\n' "$wanted"
    exit 0
fi

if ! command -v apt-get >/dev/null 2>&1; then
    printf 'This installs through apt, which this system does not have.\n' >&2
    printf 'Install Clang %s or newer however this distribution does it.\n' "$wanted" >&2
    exit 1
fi

apt_command=(apt-get)
run_as_root=()

if (( EUID != 0 )); then
    if ! command -v sudo >/dev/null 2>&1; then
        printf 'Error: sudo is required.\n' >&2
        exit 1
    fi

    apt_command=(sudo apt-get)
    run_as_root=(sudo)
fi

# LLVM's own installer, which adds the repository for this release and pulls the
# version asked for. Fetched rather than vendored: it is signed infrastructure
# that changes with each release, and a stale copy in this repository would be a
# worse thing to run than the current one.
readonly script="$(mktemp -t llvm-install-XXXXXX.sh)"
trap 'rm -f "$script"' EXIT

printf 'Fetching the LLVM installer from apt.llvm.org...\n'

if ! curl -fsSL https://apt.llvm.org/llvm.sh -o "$script"; then
    printf 'Could not fetch it. Nothing has been changed on this machine.\n' >&2
    exit 1
fi

chmod +x "$script"

if ! "${run_as_root[@]}" "$script" "$wanted"; then
    printf 'The LLVM installer failed. Nothing else here depends on it.\n' >&2
    exit 1
fi

"${apt_command[@]}" install --yes "clang-${wanted}"

printf 'clang-%s installed. `test-suite run --suites rtsan` will find it.\n' "$wanted"
