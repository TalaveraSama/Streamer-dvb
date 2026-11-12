#!/usr/bin/env bash
# Instala o actualiza TVStreammerSAT5 en Ubuntu 22.04/24.04 a partir del código
# fuente, incluyendo el servicio systemd endurecido.
#
#   sudo ./scripts/install_from_source.sh              # instala y arranca
#   sudo ./scripts/install_from_source.sh --skip-tests # no ejecuta ctest
#   sudo ./scripts/install_from_source.sh --no-start   # no arranca el servicio
#   sudo ./scripts/install_from_source.sh --prefix /opt/TVStreammerSAT5
#
# Es idempotente: reinstalarlo actualiza binarios y unidad, y NUNCA toca la
# configuración existente (tvstreammersat5-config.json, -subscribers.json, etc.).
set -euo pipefail

PREFIX="/opt/TVStreammerSAT5"
CA_PLUGIN_DIR="/opt/tvstreammersat5/ca-plugins"
DATA_DIR="/var/lib/tvstreammersat5"
SERVICE="tvstreammersat5.service"
RUN_TESTS=1
START_SERVICE=1

usage() {
    sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-tests) RUN_TESTS=0 ;;
        --no-start) START_SERVICE=0 ;;
        --prefix) PREFIX="${2:?--prefix needs a path}"; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APK_NAME="TVStreammerSAT5"

log() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }

if [[ "${EUID}" -ne 0 ]]; then
    echo "Run this installer as root: sudo $0 $*" >&2
    exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
    echo "This installer targets Ubuntu/Debian (apt-get not found)." >&2
    exit 1
fi

if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    case "${VERSION_ID:-}" in
        22.04|24.04) log "Ubuntu ${VERSION_ID} detected (supported)" ;;
        *) echo "Warning: Ubuntu ${VERSION_ID:-unknown} is untested; 22.04 and 24.04 are supported." >&2 ;;
    esac
fi

log "1/7 Installing build and runtime dependencies"
"${ROOT_DIR}/install_deps.sh"

log "2/7 Configuring CMake (Release)"
cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build" -DCMAKE_BUILD_TYPE=Release

log "3/7 Building"
cmake --build "${ROOT_DIR}/build" --parallel "$(nproc)"

if [[ "${RUN_TESTS}" -eq 1 ]]; then
    log "4/7 Running tests"
    ctest --test-dir "${ROOT_DIR}/build" --output-on-failure
    if command -v node >/dev/null 2>&1; then
        ( cd "${ROOT_DIR}" && node tests/run-all.js --quiet )
    else
        echo "node not found; skipping the source-level regression suite"
    fi
else
    log "4/7 Tests skipped (--skip-tests)"
fi

log "5/7 Installing files into ${PREFIX}"
install -d -m 0755 "$(dirname "${PREFIX}")"
install -d -m 0755 "${PREFIX}"
install -Dm755 "${ROOT_DIR}/build/${APK_NAME}" "${PREFIX}/${APK_NAME}"
mkdir -p "${CA_PLUGIN_DIR}"
if [[ -f "${ROOT_DIR}/build/tvstreammersat5-ca-newcamd.so" ]]; then
    install -Dm755 "${ROOT_DIR}/build/tvstreammersat5-ca-newcamd.so" \
        "${CA_PLUGIN_DIR}/tvstreammersat5-ca-newcamd.so"
fi

# Browser assets are served locally, without a CDN.
install -d -m 0755 "${PREFIX}/web/vendor"
for asset in mpegts.min.js hls.min.js adminlte-3.2.0.min.css LICENSE.adminlte; do
    install -m 0644 "${ROOT_DIR}/web/vendor/${asset}" "${PREFIX}/web/vendor/${asset}"
done

# The two format readers are used by the single config.txt entry point.
install -d -m 0755 "${PREFIX}/tools"
for tool in import_config.py import_astra_channels.py import_flussonic_channels.py; do
    install -m 0755 "${ROOT_DIR}/scripts/${tool}" "${PREFIX}/tools/${tool}"
done

# OSCam-mini and its seeded configuration (built by cmake/OscamMini.cmake).
if [[ -f "${ROOT_DIR}/build/oscam-mini/oscam-mini" ]]; then
    install -Dm755 "${ROOT_DIR}/build/oscam-mini/oscam-mini" \
        "${PREFIX}/oscam-mini/oscam-mini"
    if [[ -d "${ROOT_DIR}/packaging/oscam-mini/default-config" ]]; then
        mkdir -p "${PREFIX}/oscam-mini/config" "${PREFIX}/oscam-mini/default-config"
        cp -an "${ROOT_DIR}/packaging/oscam-mini/default-config/." \
            "${PREFIX}/oscam-mini/default-config/"
        # Only seed the active config when it does not exist yet.
        cp -an "${ROOT_DIR}/packaging/oscam-mini/default-config/." \
            "${PREFIX}/oscam-mini/config/"
    fi
fi

mkdir -p "${DATA_DIR}"
chmod 0755 "${DATA_DIR}"

log "6/7 Installing systemd unit and sysctl profile"
if command -v systemctl >/dev/null 2>&1; then
    install -Dm644 "${ROOT_DIR}/packaging/systemd/tvstreammersat5.service" \
        "/etc/systemd/system/${SERVICE}"
    if [[ -f "${ROOT_DIR}/packaging/oscam-mini/oscam-mini.service" ]]; then
        install -Dm644 "${ROOT_DIR}/packaging/oscam-mini/oscam-mini.service" \
            "/etc/systemd/system/oscam-mini.service"
    fi
    if [[ -f "${ROOT_DIR}/packaging/sysctl/99-tvstreammer-udp.conf" ]]; then
        install -Dm644 "${ROOT_DIR}/packaging/sysctl/99-tvstreammer-udp.conf" \
            "/etc/sysctl.d/99-tvstreammer-udp.conf"
        sysctl --system >/dev/null
    fi
    systemctl daemon-reload
else
    echo "systemctl not available; skipping service installation"
    START_SERVICE=0
fi

if [[ "${START_SERVICE}" -eq 1 ]]; then
    log "7/7 Enabling and starting ${SERVICE}"
    systemctl enable "${SERVICE}"
    systemctl restart "${SERVICE}"
    sleep 2
    # Do not claim a successful update if the restarted service failed.
    systemctl --no-pager --lines=0 status "${SERVICE}"
else
    log "7/7 Service not started (--no-start or no systemd)"
fi

cat <<EOF

Instalación completada.

  Binario:     ${PREFIX}/${APK_NAME}
  Config:      ${PREFIX}/tvstreammersat5-config.json  (se crea en el primer arranque)
  Datos:       ${DATA_DIR}
  Servicio:    ${SERVICE}

Primer arranque: el panel genera una contraseña aleatoria y la escribe UNA vez
en el log del servicio:

  journalctl -u ${SERVICE} -n 30 --no-pager

Panel: http://<ip-del-vps>:9000/  (login admin)

Antes de exponerlo a Internet lee SECURITY.md y docs/INSTALL_UBUNTU.md:
  - bind_address = 127.0.0.1  + proxy TLS (Caddy/nginx)
  - require_stream_auth = true si los players no son de confianza
  - ufw: abrir 443/tcp y NO abrir 9000/tcp
EOF
