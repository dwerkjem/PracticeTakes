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
