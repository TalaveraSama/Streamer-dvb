#!/usr/bin/env bash
# TVStreammerSAT5 host build/runtime dependencies for Ubuntu 22.04 and 24.04.
#
#   ./install_deps.sh            install everything the current target needs
#   ./install_deps.sh --check    verify an existing installation (no changes)
#   ./install_deps.sh --help
#
# 203.76: the package list is resolved against the running release instead of
# being installed blindly. Ubuntu 22.04 (Boost 1.74, OpenSSL 3.0) and 24.04
# (Boost 1.83, t64 curl/ssl runtime packages) differ in available versions, so an
# unavailable package is reported by name instead of aborting the whole install.
set -euo pipefail

MODE="install"
for argument in "$@"; do
    case "$argument" in
        --check) MODE="check" ;;
        --help|-h)
            sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown option: $argument" >&2
            exit 2
            ;;
    esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# In CI the job log is not always readable, but check annotations are. Emit the
# diagnostics that matter as single-line workflow commands (no-op elsewhere).
annotate() {
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::error title=install_deps::$(printf '%s' "$*" | tr '\n' ' ' | cut -c1-900)"
    fi
}

# --- release detection -------------------------------------------------------
DISTRO_ID="unknown"
DISTRO_VERSION="unknown"
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    DISTRO_ID="${ID:-unknown}"
    DISTRO_VERSION="${VERSION_ID:-unknown}"
fi

if [[ "$MODE" == "install" ]]; then
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "This installer requires an apt-based Ubuntu/Debian system (detected: $DISTRO_ID $DISTRO_VERSION)." >&2
        exit 1
    fi
    case "$DISTRO_VERSION" in
        22.04|24.04) ;;
        *) echo "Warning: Ubuntu $DISTRO_VERSION is untested; 22.04 and 24.04 are supported." >&2 ;;
    esac
fi

SUDO=()
if [[ "${EUID}" -ne 0 ]]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "Run as root or install sudo first." >&2
        exit 1
    fi
    SUDO=(sudo)
fi

# --- package sets ------------------------------------------------------------
# Required to configure and build the current CMake target.
REQUIRED_PACKAGES=(
    build-essential
    cmake
    pkg-config
    libgstreamer1.0-dev
    libgstreamer-plugins-base1.0-dev
    libgstreamer-plugins-bad1.0-dev
    libcurl4-openssl-dev
    libjsoncpp-dev
    libssl-dev
    libcrypt-dev
    libdvbcsa-dev
    libboost-system-dev
    libboost-thread-dev
    libpcsclite-dev
    # Pulled in by libgstreamer1.0-dev on jammy (22.04); listed explicitly so the
    # availability check and the reconciliation below cover it.
    libunwind-dev
)

# Required to run the panel with the protocols the UI exposes.
RUNTIME_PACKAGES=(
    ca-certificates
    gstreamer1.0-tools
    gstreamer1.0-plugins-base
    gstreamer1.0-plugins-good
    gstreamer1.0-plugins-bad
    gstreamer1.0-plugins-ugly
    gstreamer1.0-libav
    gstreamer1.0-rtsp
)

# Nice to have: hardware acceleration and PC/SC smart-card tooling.
OPTIONAL_PACKAGES=(
    gstreamer1.0-vaapi
    vainfo
    intel-media-va-driver
    pcscd
    pcsc-tools
)

package_available() {
    apt-cache show "$1" >/dev/null 2>&1
}

# --- check mode --------------------------------------------------------------
check_environment() {
    local failures=0
    echo "Checking TVStreammerSAT5 build environment on $DISTRO_ID $DISTRO_VERSION..."

    for command in g++ cmake pkg-config; do
        if command -v "$command" >/dev/null 2>&1; then
            echo "  ok    $command"
        else
            echo "  MISS  $command"
            failures=$((failures + 1))
        fi
    done

    for module in gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0 gstreamer-mpegts-1.0 libcurl jsoncpp openssl; do
        if pkg-config --exists "$module" 2>/dev/null; then
            echo "  ok    pkg-config $module $(pkg-config --modversion "$module" 2>/dev/null)"
        else
            echo "  MISS  pkg-config $module"
            failures=$((failures + 1))
        fi
    done

    for header in /usr/include/dvbcsa/dvbcsa.h /usr/include/boost/version.hpp; do
        if [[ -e "$header" ]]; then
            echo "  ok    $header"
        else
            echo "  MISS  $header"
            failures=$((failures + 1))
        fi
    done

    if [[ -x "${ROOT_DIR}/scripts/check_transcoder_plugins.sh" ]]; then
        echo "Checking GStreamer elements..."
        if "${ROOT_DIR}/scripts/check_transcoder_plugins.sh"; then
            echo "  ok    transcoder elements"
        else
            echo "  MISS  one or more transcoder elements"
            failures=$((failures + 1))
        fi
    fi

    if [[ "$failures" -ne 0 ]]; then
        echo "$failures check(s) failed. Run ./install_deps.sh to install the missing pieces." >&2
        return 1
    fi
    echo "Environment looks complete."
    return 0
}

if [[ "$MODE" == "check" ]]; then
    check_environment
    exit $?
fi

# --- install mode ------------------------------------------------------------
# DPkg::Lock::Timeout: unattended-upgrades can hold the dpkg lock on cloud
# images and make `apt-get install` fail with exit code 100.
APT_GET=("${SUDO[@]}" apt-get -o DPkg::Lock::Timeout=180)
export DEBIAN_FRONTEND=noninteractive

echo "Installing TVStreammerSAT5 dependencies on $DISTRO_ID $DISTRO_VERSION..."
"${APT_GET[@]}" update

TO_INSTALL=()
MISSING_REQUIRED=()
for package in "${REQUIRED_PACKAGES[@]}" "${RUNTIME_PACKAGES[@]}"; do
    if package_available "$package"; then
        TO_INSTALL+=("$package")
    else
        MISSING_REQUIRED+=("$package")
    fi
done

# 203.76: on jammy, libgstreamer1.0-dev depends on libunwind-dev, which pins
# libunwind8 to the exact archive version. Cloud/CI images often ship a newer
# libunwind8 for other tooling, and apt then refuses the whole transaction with
# "held broken packages" without naming the real culprit. Align libunwind first.
reconcile_libunwind() {
    if ! package_available libunwind-dev; then
        return 0
    fi
    if "${APT_GET[@]}" install -y --no-install-recommends libunwind-dev >/dev/null 2>&1; then
        return 0
    fi
    echo "libunwind-dev needs a different libunwind8; retrying with --allow-downgrades"
    "${APT_GET[@]}" install -y --no-install-recommends --allow-downgrades libunwind-dev \
        || echo "Warning: could not reconcile libunwind; the build may fail." >&2
}
reconcile_libunwind

echo "Resolved: required+runtime=${#TO_INSTALL[@]} available, missing=${#MISSING_REQUIRED[@]}"
if [[ "${#MISSING_REQUIRED[@]}" -ne 0 ]]; then
    annotate "missing required packages on ${DISTRO_ID} ${DISTRO_VERSION}: ${MISSING_REQUIRED[*]}"
    # Printed on stdout as well: stdout is flushed at exit, so CI logs always end
    # with the reason instead of burying it between apt progress lines.
    echo "MISSING_REQUIRED_PACKAGES: ${MISSING_REQUIRED[*]}"
    echo "These required packages are not available on $DISTRO_ID $DISTRO_VERSION:" >&2
    printf '  - %s\n' "${MISSING_REQUIRED[@]}" >&2
    echo "Enable the 'universe' component (sudo add-apt-repository universe) and retry." >&2
    exit 1
fi

echo "Required packages (${#TO_INSTALL[@]}): ${TO_INSTALL[*]}"
if ! "${APT_GET[@]}" install -y --no-install-recommends "${TO_INSTALL[@]}"; then
    # Re-run the simulation to capture the dependency conflict in a compact form
    # that survives log truncation, and surface it as a CI annotation.
    echo "APT_INSTALL_FAILED: the required package set could not be installed."
    simulation="$("${APT_GET[@]}" -s install "${TO_INSTALL[@]}" 2>&1 \
        | grep -E "Depends:|Conflicts:|Breaks:|E:" | head -8 || true)"
    printf '%s\n' "${simulation}"
    annotate "apt simulate failed: ${simulation}"
    annotate "attempted set: ${TO_INSTALL[*]}"
    exit 1
fi

# Optional packages are installed in a second, non-fatal pass: a broken
# dependency or a conflict between VA-API/NVENC extras on one release must not
# abort an otherwise valid installation.
OPTIONAL_AVAILABLE=()
SKIPPED_OPTIONAL=()
for package in "${OPTIONAL_PACKAGES[@]}"; do
    if package_available "$package"; then
        OPTIONAL_AVAILABLE+=("$package")
    else
        SKIPPED_OPTIONAL+=("$package")
    fi
done

if [[ "${#SKIPPED_OPTIONAL[@]}" -ne 0 ]]; then
    echo "Optional packages not available on this release (skipped): ${SKIPPED_OPTIONAL[*]}"
fi
if [[ "${#OPTIONAL_AVAILABLE[@]}" -ne 0 ]]; then
    echo "Optional packages (${#OPTIONAL_AVAILABLE[@]}): ${OPTIONAL_AVAILABLE[*]}"
    if ! "${APT_GET[@]}" install -y --no-install-recommends "${OPTIONAL_AVAILABLE[@]}"; then
        echo "Warning: these optional packages could not be installed: ${OPTIONAL_AVAILABLE[*]}" >&2
        echo "         The build does not require them; hardware acceleration or" >&2
        echo "         smart-card tooling will be unavailable." >&2
    fi
fi

"${APT_GET[@]}" clean

if [[ -x "${ROOT_DIR}/scripts/check_transcoder_plugins.sh" ]]; then
    "${ROOT_DIR}/scripts/check_transcoder_plugins.sh"
fi

echo
echo "Dependencies installed."
echo "Build with:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build --parallel \"\$(nproc)\""
echo "  ctest --test-dir build --output-on-failure"
echo
echo "Run: ./build/TVStreammerSAT5 (first start prints a generated admin password)."
echo "Deployment, TLS and systemd notes: docs/INSTALL_UBUNTU.md"
