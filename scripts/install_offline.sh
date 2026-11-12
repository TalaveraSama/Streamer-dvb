#!/usr/bin/env bash
# Offline binary bundle installer. Never installs packages or starts services.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP=/opt/TVStreammerSAT5
SERVICE=tvstreammersat5.service
MODE=${1:---check}
if [[ $# -gt 1 || ( "$MODE" != --check && "$MODE" != --install ) ]]; then
    echo "Uso: bash install_offline.sh [--check|--install]" >&2
    exit 2
fi
fail() { echo "ERROR: $*" >&2; exit 1; }
[[ -f "$HERE/PLATFORM" ]] || fail "Falta PLATFORM en el paquete."
read -r EXPECTED_ID EXPECTED_VERSION EXPECTED_ARCH < "$HERE/PLATFORM"
[[ -r /etc/os-release ]] || fail "No se puede determinar el sistema operativo."
# shellcheck disable=SC1091
. /etc/os-release
[[ "${ID:-}" == "$EXPECTED_ID" && "${VERSION_ID:-}" == "$EXPECTED_VERSION" && "$(uname -m)" == "$EXPECTED_ARCH" ]] || \
    fail "Este ZIP es para $EXPECTED_ID $EXPECTED_VERSION $EXPECTED_ARCH; detectado ${ID:-?} ${VERSION_ID:-?} $(uname -m)."
command -v ldd >/dev/null || fail "Falta ldd para comprobar bibliotecas."
for binary in TVStreammerSAT5 tvstreammersat5-ca-newcamd.so oscam-mini/oscam-mini; do
    [[ -f "$HERE/$binary" ]] || fail "Falta $binary en el ZIP."
    output=$(LC_ALL=C ldd "$HERE/$binary") || fail "No se pudo comprobar $binary con ldd."
    if grep -q 'not found' <<< "$output"; then
        echo "$output" >&2
        fail "Faltan bibliotecas para $binary. Instálalas antes de continuar."
    fi
done
for file in web/vendor/mpegts.min.js web/vendor/hls.min.js web/vendor/adminlte-3.2.0.min.css web/vendor/LICENSE.adminlte packaging/systemd/tvstreammersat5.service packaging/oscam-mini/oscam-mini.service; do
    [[ -f "$HERE/$file" ]] || fail "Falta $file en el ZIP."
done
command -v gst-inspect-1.0 >/dev/null || fail "Falta gst-inspect-1.0; instala las dependencias GStreamer."
echo "Comprobación básica de bibliotecas y plataforma: correcta."
echo "Los plugins GStreamer para cada protocolo, PC/SC y el hardware requieren verificación adicional."
if [[ "$MODE" == --check ]]; then exit 0; fi
[[ $EUID -eq 0 ]] || fail "Ejecuta --install como root."
command -v systemctl >/dev/null || fail "Se requiere systemd."
# Never replace a running binary or interfere with the existing service.
if systemctl is-active --quiet "$SERVICE"; then
    fail "El servicio está activo. Deténlo explícitamente antes de instalar; esto interrumpe los canales."
fi
if systemctl is-active --quiet oscam-mini.service; then
    fail "OSCam-mini está activo. Deténlo explícitamente antes de instalar; puede interrumpir la decodificación."
fi
# Preserve original unit files/overrides and all existing application data.
umask 077
backup=$(mktemp -d /root/tvs-offline-backup.XXXXXX)
if [[ -d "$APP" ]]; then
    items=()
    for path in TVStreammerSAT5 tvstreammersat5-config.json tvstreammersat5-ui.key tvstreammersat5-subscribers.json oscam-mini web/vendor tools; do
        [[ ! -e "$APP/$path" ]] || items+=("$path")
    done
    if (( ${#items[@]} )); then
        tar -C "$APP" -czf "$backup/app.tar.gz" "${items[@]}"
        tar -tzf "$backup/app.tar.gz" >/dev/null
    fi
fi
if [[ -f /opt/tvstreammersat5/ca-plugins/tvstreammersat5-ca-newcamd.so ]]; then
    cp -p /opt/tvstreammersat5/ca-plugins/tvstreammersat5-ca-newcamd.so "$backup/"
fi
echo "Respaldo privado: $backup"
install -d -m 0755 "$APP" "$APP/web/vendor" "$APP/tools" "$APP/oscam-mini" /opt/tvstreammersat5/ca-plugins /var/lib/tvstreammersat5
install -m 0755 "$HERE/TVStreammerSAT5" "$APP/TVStreammerSAT5.new"
mv -f "$APP/TVStreammerSAT5.new" "$APP/TVStreammerSAT5"
install -m 0755 "$HERE/tvstreammersat5-ca-newcamd.so" /opt/tvstreammersat5/ca-plugins/tvstreammersat5-ca-newcamd.so
install -m 0755 "$HERE/oscam-mini/oscam-mini" "$APP/oscam-mini/oscam-mini"
for f in mpegts.min.js hls.min.js adminlte-3.2.0.min.css LICENSE.adminlte; do install -m 0644 "$HERE/web/vendor/$f" "$APP/web/vendor/$f"; done
for f in import_config.py import_astra_channels.py import_flussonic_channels.py; do install -m 0755 "$HERE/tools/$f" "$APP/tools/$f"; done
mkdir -p "$APP/oscam-mini/config" "$APP/oscam-mini/default-config"
cp -an "$HERE/packaging/oscam-mini/default-config/." "$APP/oscam-mini/config/"
cp -an "$HERE/packaging/oscam-mini/default-config/." "$APP/oscam-mini/default-config/"
for service in "$SERVICE" oscam-mini.service; do
    load=$(systemctl show -p LoadState --value "$service")
    if [[ "$load" == not-found ]]; then
        if [[ "$service" == "$SERVICE" ]]; then file=packaging/systemd/tvstreammersat5.service; else file=packaging/oscam-mini/oscam-mini.service; fi
        install -m 0644 "$HERE/$file" "/etc/systemd/system/$service"
    else
        echo "Unidad existente conservada: $service (revisa que sus rutas sean $APP)."
    fi
done
systemctl daemon-reload
echo "Instalación terminada. No se ha iniciado ni habilitado ningún servicio."
echo "Comprueba unidades, configuración y dependencias antes de iniciar manualmente."
