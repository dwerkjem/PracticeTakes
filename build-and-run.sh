#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_ROOT}/build}"
TARGET_NAME="${TARGET_NAME:-PracticeTakes}"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf -- "$BUILD_DIR"
    shift
fi

if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    printf 'Error: %s does not contain CMakeLists.txt.\n' "$PROJECT_ROOT" >&2
    exit 1
fi

cmake_args=(
    -S "$PROJECT_ROOT"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
    cmake_args+=(
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    )
elif command -v vcpkg >/dev/null 2>&1; then
    detected_vcpkg_root="$(cd -- "$(dirname -- "$(command -v vcpkg)")" && pwd)"

    if [[ -f "$detected_vcpkg_root/scripts/buildsystems/vcpkg.cmake" ]]; then
        cmake_args+=(
            -DCMAKE_TOOLCHAIN_FILE="$detected_vcpkg_root/scripts/buildsystems/vcpkg.cmake"
        )
    fi
fi

printf 'Configuring Practice Takes (%s)...\n' "$BUILD_TYPE"
cmake "${cmake_args[@]}"

printf 'Building %s...\n' "$TARGET_NAME"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --target "$TARGET_NAME" --parallel

executable_candidates=(
    "$BUILD_DIR/$TARGET_NAME"
    "$BUILD_DIR/$TARGET_NAME.exe"
    "$BUILD_DIR/$BUILD_TYPE/$TARGET_NAME"
    "$BUILD_DIR/$BUILD_TYPE/$TARGET_NAME.exe"
)

for executable in "${executable_candidates[@]}"; do
    if [[ -x "$executable" ]]; then
        printf 'Running %s...\n' "$executable"
        exec "$executable" "$@"
    fi
done

printf 'Error: build succeeded, but the %s executable was not found.\n' "$TARGET_NAME" >&2
printf 'Set TARGET_NAME if the CMake executable target uses a different name.\n' >&2
exit 1
