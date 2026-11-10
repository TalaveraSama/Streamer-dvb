# Streamer-dvb

Copia de trabajo de **TVStreammerSAT5** (servidor de recepción y distribución de TV
DVB-S/S2 + red, C++17/GStreamer) más el archivo histórico de su predecesor
**TVStreamer5**.

Este repositorio **no es un fork de GitHub**: es una copia del árbol de
`Monkipnet/Tvstreamer_sat` en el commit `f9af8f6` (2026-09-27) y de los estados
antiguos de `Monkipnet/TVStreamer5`. La historia de Git de los originales **no**
está incluida; vive en los repos del autor.

## Contenido

| Ruta | Qué es |
|---|---|
| `src/`, `web/`, `third_party/`, `packaging/`, `scripts/`, `docs/`, `tests/`, `examples/` | Código y recursos de TVStreammerSAT5 203.75 (copia de `f9af8f6`) |
| `CMakeLists.txt`, `Dockerfile`, `install_deps.sh`, `README.md` | Build y documentación del mismo commit |
| `legacy/tvstreamer5/v115/` | Árbol completo de TVStreamer5 en `v115` (`2167f64`, 2026-08-12), **recuperado** del commit anterior al `Fresh start` que vació `main` |
| `legacy/tvstreamer5/snapshots/agent-2026-08-03/` | Snapshot de la rama `agent-multi-output-stream-formats` (`b58588f`) |
| `legacy/tvstreamer5/snapshots/codespace-2026-07-14/` | Snapshot de la rama `codespace-fantastic-goggles-967jx959wvjgh9rwg` (`a17cc99`) |
| `legacy/tvstreamer5/RESTORE.md` | Qué contiene cada snapshot, SHAs y cómo regenerarlos |
| `docs-reviews/` | Revisión técnica de los dos repos, hallazgos y plan de trabajo |
| `NOTICE.md` | Atribución, licencias y estado legal |

Los `.apk` (3,4 MB cada uno) que acompañaban a TVStreamer5 **no** se copiaron:
siguen en el historial de `Monkipnet/TVStreamer5` y no aportan nada al código.

## Build (idéntico al upstream)

```bash
./install_deps.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

o con contenedor:

```bash
docker build -t tvstreammersat5 .
docker run --rm -p 9000:9000 tvstreammersat5
```

El panel queda en `http://IP:9000/` con `admin` / `admin` por defecto —
**cámbialo antes de exponerlo** (ver `docs-reviews/REVISION_Tvstreamer_sat.md`, § 5).

## Sincronizar con el upstream

```bash
git remote add upstream https://github.com/Monkipnet/Tvstreamer_sat.git
git fetch upstream
git diff --stat upstream/main        # ver qué cambió
```

## Advertencia legal

El proyecto original **no tiene LICENSE**: el copyright es de su autor. Esta copia
se mantiene para uso y modificación local. **No redistribuyas binarios derivados**
sin autorización de `Monkipnet` y revisa `NOTICE.md` antes de publicar nada.
