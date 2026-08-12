#!/usr/bin/env bash

set -euo pipefail

mode=prompt

while (( $# > 0 )); do
    case "$1" in
        --install)
            mode=install
            ;;
        --check-only)
            mode=check
            ;;
        --help)
            cat <<'EOF'
Usage: check-linux-build-dependencies.sh [--install | --check-only]

Checks the Debian/Ubuntu packages required to build Practice Takes.

  --install     Install missing packages without an additional prompt.
  --check-only  Report missing packages and exit without prompting.
EOF
            exit 0
            ;;
        *)
            printf 'Error: unknown option: %s\n' "$1" >&2
            exit 2
            ;;
    esac
    shift
done

if [[ "$(uname -s)" != "Linux" ]]; then
    exit 0
fi

if ! command -v apt-get >/dev/null 2>&1 || ! command -v dpkg-query >/dev/null 2>&1; then
    printf 'Automatic dependency checks currently support Debian and Ubuntu.\n' >&2
    printf 'Install the compiler, CMake, Git, pkg-config, and JUCE platform libraries for your distribution.\n' >&2
    exit 1
fi

readonly apt_packages=(
    build-essential
    ca-certificates
    cmake
    curl
    dpkg-dev
    file
    git
    ninja-build
    pkg-config
    tar
    unzip
    zip
    libasound2-dev
    libcurl4-openssl-dev
    libfontconfig1-dev
    libfreetype6-dev
    libgl1-mesa-dev
    libgtk-3-dev
    libjack-jackd2-dev
    libx11-dev
    libxcomposite-dev
    libxcursor-dev
    libxext-dev
    libxinerama-dev
    libxrandr-dev
    libxrender-dev
    # Not needed to build or run the application. `test-suite capture --headless`
    # photographs surfaces on a screen of its own so a capture run does not take
    # over the desktop, and Xvfb is that screen.
    xvfb
)

missing_packages=()

for package in "${apt_packages[@]}"; do
    status="$(dpkg-query --show --showformat='${Status}' "$package" 2>/dev/null || true)"
    if [[ "$package" == libfreetype6-dev && "$status" != "install ok installed" ]]; then
        # Debian 13 renamed the development package while retaining
        # libfreetype6-dev as an apt compatibility alias.
        status="$(dpkg-query --show --showformat='${Status}' libfreetype-dev 2>/dev/null || true)"
    fi
    if [[ "$status" != "install ok installed" ]]; then
        missing_packages+=("$package")
    fi
done

if (( ${#missing_packages[@]} == 0 )); then
    printf 'Linux build dependencies are installed.\n'
    exit 0
fi

printf 'Missing Linux build dependencies:\n' >&2
printf '  %s\n' "${missing_packages[@]}" >&2

should_install=false

case "$mode" in
    install)
        should_install=true
        ;;
    check)
        ;;
    prompt)
        if [[ -t 0 ]]; then
            read -r -p 'Install the missing packages now? [y/N] ' reply
            case "$reply" in
                y | Y | yes | YES | Yes)
                    should_install=true
                    ;;
            esac
        else
            printf 'Re-run with --install to authorize installation in a noninteractive shell.\n' >&2
        fi
        ;;
esac

if [[ "$should_install" != true ]]; then
    printf 'Dependency installation was not authorized; build cancelled.\n' >&2
    exit 1
fi

apt_command=(apt-get)
if (( EUID != 0 )); then
    if ! command -v sudo >/dev/null 2>&1; then
        printf 'Error: sudo is required to install the missing packages.\n' >&2
        exit 1
    fi
    apt_command=(sudo apt-get)
fi

"${apt_command[@]}" update
"${apt_command[@]}" install --yes "${missing_packages[@]}"

printf 'Linux build dependencies installed successfully.\n'

# --- RealtimeSanitizer's compiler -------------------------------------------
#
# Separate from the list above, and deliberately not fatal. RealtimeSanitizer
# arrived in Clang 20, and Debian 13 ships 19 -- so this needs a repository the
# distribution does not have, which is a bigger thing to do to somebody's
# machine than installing a package they already have access to.
#
# Nothing here is required to build or test Practice Takes. It buys one thing:
# running the audio-callback check locally instead of waiting for the pull
# request. It runs in CI on every pull request either way.
if command -v clang++-21 >/dev/null 2>&1 || command -v clang++-20 >/dev/null 2>&1; then
    printf 'RealtimeSanitizer: a suitable Clang is already installed.\n'
    exit 0
fi

if [[ "$mode" != install ]]; then
    exit 0
fi

codename="$( . /etc/os-release 2>/dev/null && printf '%s' "${VERSION_CODENAME:-}" )"

if [[ -z "$codename" ]]; then
    printf 'RealtimeSanitizer: no release codename, so no LLVM repository to add. Skipping.\n'
    exit 0
fi

printf '\nRealtimeSanitizer needs Clang 20 and this system has none.\n'
printf 'It is optional: nothing else here needs it, and the check runs in CI regardless.\n'
printf 'Installing it adds the LLVM apt repository (apt.llvm.org) to this machine.\n'

install_clang=false

if [[ -t 0 ]]; then
    read -r -p 'Add the LLVM repository and install clang-20? [y/N] ' reply
    case "$reply" in
        y | Y | yes | YES | Yes) install_clang=true ;;
    esac
else
    printf 'Not adding it in a noninteractive shell. Run tools/scripts/build/install-clang-20.sh to.\n'
fi

if [[ "$install_clang" != true ]]; then
    exit 0
fi

if ! "$(dirname "${BASH_SOURCE[0]}")/install-clang-20.sh"; then
    # A failure here is not a failed dependency install: everything that
    # matters went in above.
    printf 'RealtimeSanitizer: clang-20 could not be installed. Everything else is ready.\n' >&2
fi
