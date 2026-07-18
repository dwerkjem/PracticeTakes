#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
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
            default_vcpkg_triplet="x64-linux-practice"
            ;;
        aarch64 | arm64)
            default_vcpkg_triplet="arm64-linux-practice"
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
            -DVCPKG_OVERLAY_TRIPLETS="$PROJECT_ROOT/triplets"
        )

        if [[ "$(uname -m)" == "x86_64" ]]; then
            cmake_args+=(
                -DVCPKG_HOST_TRIPLET="${VCPKG_HOST_TRIPLET:-x64-linux-practice}"
            )
        fi

        vcpkg_prefix="$vcpkg_installed_dir/$vcpkg_triplet"
        vcpkg_pkgconfig_paths=(
            "$vcpkg_prefix/lib/pkgconfig"
            "$vcpkg_prefix/share/pkgconfig"
        )

        joined_pkgconfig_path="$(
            IFS=:
            printf '%s' "${vcpkg_pkgconfig_paths[*]}"
        )"

        export PKG_CONFIG_PATH="$joined_pkgconfig_path${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
        export CMAKE_PREFIX_PATH="$vcpkg_prefix${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
        export CMAKE_INCLUDE_PATH="$vcpkg_prefix/include${CMAKE_INCLUDE_PATH:+:$CMAKE_INCLUDE_PATH}"
        export CMAKE_LIBRARY_PATH="$vcpkg_prefix/lib${CMAKE_LIBRARY_PATH:+:$CMAKE_LIBRARY_PATH}"
        export CPATH="$vcpkg_prefix/include${CPATH:+:$CPATH}"
        export LIBRARY_PATH="$vcpkg_prefix/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
        export LD_LIBRARY_PATH="$vcpkg_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

        # JUCE configures juceaide in a nested CMake project that does not
        # inherit the outer project's toolchain settings. Environment flags
        # are inherited by both CMake invocations and by Nix's compiler wrapper.
        juce_platform_defines="-DJUCE_USE_XCURSOR=0 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0"
        export CFLAGS="-I$vcpkg_prefix/include ${CFLAGS:-}"
        export CXXFLAGS="-I$vcpkg_prefix/include $juce_platform_defines ${CXXFLAGS:-}"
        export LDFLAGS="-L$vcpkg_prefix/lib -Wl,-rpath,$vcpkg_prefix/lib ${LDFLAGS:-}"
    fi
fi

printf 'Configuring Practice Takes (%s)...\n' "$BUILD_TYPE"
cmake "${cmake_args[@]}"

if [[ -n "${vcpkg_prefix:-}" && "$(uname -s)" == "Linux" ]]; then
    for library_pattern in 'libX11.so*' 'libXext.so*'; do
        if ! compgen -G "$vcpkg_prefix/lib/$library_pattern" >/dev/null; then
            printf 'Error: vcpkg did not install shared %s libraries.\n' "$library_pattern" >&2
            printf 'Run ./build-and-run.sh --clean after pulling the latest triplet.\n' >&2
            exit 1
        fi
    done
fi

printf 'Building %s...\n' "$TARGET_NAME"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --target "$TARGET_NAME" --parallel

if [[ "$BUILD_ONLY" == true ]]; then
    exit 0
fi

executable_candidates=(
    "$BUILD_DIR/bin/$TARGET_NAME"
    "$BUILD_DIR/bin/$TARGET_NAME.exe"
    "$BUILD_DIR/$TARGET_NAME"
    "$BUILD_DIR/$TARGET_NAME.exe"
    "$BUILD_DIR/$BUILD_TYPE/$TARGET_NAME"
    "$BUILD_DIR/$BUILD_TYPE/$TARGET_NAME.exe"
)

if [[ "$(uname -s)" == "Linux" && -z "${DISPLAY:-}" ]]; then
    printf 'Error: DISPLAY is not set, so JUCE cannot open its X11 window.\n' >&2

    if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
        printf 'This JUCE build requires XWayland to run in a Wayland session.\n' >&2
    fi

    exit 1
fi

for executable in "${executable_candidates[@]}"; do
    if [[ -x "$executable" ]]; then
        printf 'Running %s...\n' "$executable"
        exec "$executable" "${program_args[@]}"
    fi
done

printf 'Error: build succeeded, but the %s executable was not found.\n' "$TARGET_NAME" >&2
printf 'Set TARGET_NAME if the CMake executable target uses a different name.\n' >&2
exit 1
