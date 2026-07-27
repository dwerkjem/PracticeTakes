#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
app="${repo_root}/build/bin/PracticeTakes"
cache="${repo_root}/build/CMakeCache.txt"

if [[ ! -x "${app}" ]]; then
    echo "PracticeTakes is not built. Run the Build Practice Takes task first." >&2
    exit 1
fi

if [[ ! -f "${cache}" ]] ||
    ! grep -qx "PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB:BOOL=ON" "${cache}"; then
    echo "Performance Lab is disabled. Reconfigure with:" >&2
    echo "  cmake -S . -B build -DPRACTICE_TAKES_ENABLE_PERFORMANCE_LAB=ON" >&2
    exit 1
fi

exec "${app}" --automate-performance-lab
