#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
RELEASES_DIR="${ROOT_DIR}/releases"

require_command() {
    local command_name="$1"

    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "ERROR: '${command_name}' is required but was not found in PATH."
        exit 1
    fi
}

detect_platform() {
    case "$(uname -s)" in
        Darwin)
            PLATFORM_NAME="macOS"
            DEFAULT_GENERATOR="Unix Makefiles"
            JOB_COUNT="$(sysctl -n hw.ncpu)"
            ;;
        Linux)
            PLATFORM_NAME="Linux"
            if command -v ninja >/dev/null 2>&1; then
                DEFAULT_GENERATOR="Ninja"
            else
                DEFAULT_GENERATOR="Unix Makefiles"
            fi
            JOB_COUNT="$(nproc)"
            ;;
        *)
            echo "ERROR: Unsupported platform '$(uname -s)'. This script supports macOS and Linux."
            exit 1
            ;;
    esac
}

validate_linux_dependencies() {
    if [[ "${PLATFORM_NAME}" != "Linux" ]]; then
        return
    fi

    require_command pkg-config

    if ! pkg-config --exists gtk+-x11-3.0; then
        echo "ERROR: Missing Linux dependency 'gtk+-x11-3.0'."
        exit 1
    fi

    if ! pkg-config --exists webkit2gtk-4.1; then
        echo "ERROR: Missing Linux dependency 'webkit2gtk-4.1'."
        exit 1
    fi
}

require_command cmake
detect_platform
validate_linux_dependencies

GENERATOR="${CMAKE_GENERATOR:-${DEFAULT_GENERATOR}}"
JOB_COUNT="${OXYGEN_BUILD_JOBS:-${JOB_COUNT}}"

echo "Configuring oXygen for ${PLATFORM_NAME}..."
echo "Generator: ${GENERATOR}"
echo "Parallel jobs: ${JOB_COUNT}"

echo "Cleaning previous builds..."
rm -rf "${BUILD_DIR}" "${RELEASES_DIR}"
mkdir -p "${RELEASES_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -G "${GENERATOR}"

echo "Building oXygen (Release)..."
cmake --build "${BUILD_DIR}" --config Release -j "${JOB_COUNT}"

echo "Build successful."
echo "Artifacts:"
ls -d "${RELEASES_DIR}"/*.vst3 2>/dev/null || echo "  (Release VST3 not found)"
