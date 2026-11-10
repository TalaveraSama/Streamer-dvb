#!/usr/bin/env bash
# Exporta un ref (rama o commit) de Monkipnet/TVStreamer5 al árbol local.
#
#   ./export-branch.sh <destino> [ref]
#
# Ejemplos:
#   ./export-branch.sh v115 2167f64
#   ./export-branch.sh codespace codespace-fantastic-goggles-967jx959wvjgh9rwg
#   ./export-branch.sh agent agent-multi-output-stream-formats
#
# Por defecto exporta `main`. El APK de 3,4 MB se omite siempre.
set -euo pipefail

UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/Monkipnet/TVStreamer5.git}"
DEST="${1:?uso: $0 <destino> [ref]}"
REF="${2:-main}"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE="${TV5_CACHE:-${TMPDIR:-/tmp}/tvstreamer5-upstream}"

if [[ ! -d "$CACHE/.git" ]]; then
    echo "Clonando $UPSTREAM_URL en $CACHE ..."
    git clone --quiet --no-checkout "$UPSTREAM_URL" "$CACHE"
else
    git -C "$CACHE" fetch --quiet --all --prune
fi

if ! git -C "$CACHE" rev-parse --verify "$REF^{commit}" >/dev/null 2>&1; then
    echo "El ref '$REF' no existe en $UPSTREAM_URL" >&2
    echo "Refs disponibles:" >&2
    git -C "$CACHE" branch -r >&2
    exit 1
fi

TARGET="$HERE/$DEST"
rm -rf "$TARGET"
mkdir -p "$TARGET"
git -C "$CACHE" archive "$REF" | tar -x -C "$TARGET"
find "$TARGET" -name '*.apk' -delete

COMMIT="$(git -C "$CACHE" rev-parse --short "$REF")"
echo "Exportado $REF ($COMMIT) -> ${TARGET#$PWD/}"
echo "Archivos: $(find "$TARGET" -type f | wc -l)"
