#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_ROOT}/build}"
TARGET_NAME="${TARGET_NAME:-PracticeTakes}"
BUILD_ONLY=false
CLEAN=false
program_args=()

while (( $# > 0 )); do
    case "$1" in
        --build-only)
            BUILD_ONLY=true
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --)
            shift
            program_args+=("$@")
            break
            ;;
        *)
            program_args+=("$1")
            shift
            ;;
    esac
done

if [[ "$CLEAN" == true ]]; then
    rm -rf -- "$BUILD_DIR"
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

vcpkg_toolchain=""

if [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
    vcpkg_toolchain="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
elif command -v vcpkg >/dev/null 2>&1; then
    detected_vcpkg_root="$(cd -- "$(dirname -- "$(command -v vcpkg)")" && pwd)"

    if [[ -f "$detected_vcpkg_root/scripts/buildsystems/vcpkg.cmake" ]]; then
        vcpkg_toolchain="$detected_vcpkg_root/scripts/buildsystems/vcpkg.cmake"
    fi
fi

if [[ -n "$vcpkg_toolchain" ]]; then
    case "$(uname -m)" in
        x86_64)
            default_vcpkg_triplet="x64-linux"
            ;;
        aarch64 | arm64)
            default_vcpkg_triplet="arm64-linux"
            ;;
        *)
            default_vcpkg_triplet=""
            ;;
    esac

    vcpkg_triplet="${VCPKG_TARGET_TRIPLET:-$default_vcpkg_triplet}"
    vcpkg_installed_dir="${VCPKG_INSTALLED_DIR:-$BUILD_DIR/vcpkg_installed}"

    cmake_args+=(
        -DCMAKE_TOOLCHAIN_FILE="$vcpkg_toolchain"
    )

    if [[ -n "$vcpkg_triplet" ]]; then
        cmake_args+=(
            -DVCPKG_TARGET_TRIPLET="$vcpkg_triplet"
        )

        vcpkg_pkgconfig_paths=(
            "$vcpkg_installed_dir/$vcpkg_triplet/lib/pkgconfig"
            "$vcpkg_installed_dir/$vcpkg_triplet/share/pkgconfig"
        )

        joined_pkgconfig_path="$(
            IFS=:
            printf '%s' "${vcpkg_pkgconfig_paths[*]}"
        )"

        export PKG_CONFIG_PATH="$joined_pkgconfig_path${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    fi
fi

printf 'Configuring Practice Takes (%s)...\n' "$BUILD_TYPE"
cmake "${cmake_args[@]}"

printf 'Building %s...\n' "$TARGET_NAME"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --target "$TARGET_NAME" --parallel

if [[ "$BUILD_ONLY" == true ]]; then
    exit 0
fi

executable_candidates=(
    "$BUILD_DIR/$TARGET_NAME"
    "$BUILD_DIR/$TARGET_NAME.exe"
    "$BUILD_DIR/$BUILD_TYPE/$TARGET_NAME"
    "$BUILD_DIR/$BUILD_TYPE/$TARGET_NAME.exe"
)

for executable in "${executable_candidates[@]}"; do
    if [[ -x "$executable" ]]; then
        printf 'Running %s...\n' "$executable"
        exec "$executable" "${program_args[@]}"
    fi
done

printf 'Error: build succeeded, but the %s executable was not found.\n' "$TARGET_NAME" >&2
printf 'Set TARGET_NAME if the CMake executable target uses a different name.\n' >&2
exit 1
