# Revisión — `Monkipnet/TVStreamer5` (ramas) y estado del fork

**Fecha:** 2026-11-10
**Clon:** `/home/user/TVStreamer5`
**Commit por rama:** `main` = `06ae075`, `agent-multi-output-stream-formats` = `b58588f`, `codespace-fantastic-goggles-967jx959wvjgh9rwg` = `a17cc99`
**Total commits del repo:** 254 · **Tags:** 0 · **Releases:** 0

---

## 1. Primero: el fork y la "actualización"

Revisado desde este entorno (token de GitHub App sobre `TalaveraSama`) **y sin autenticar** (para descartar que el token estuviera ocultando repos públicos):

| Comprobación | Resultado |
|---|---|
| `TalaveraSama/Tvstreamer_sat` | **404** (con y sin auth) |
| `TalaveraSama/TVStreamer5` | **404** (con y sin auth) |
| Repos con `fork=true` en tu cuenta | **0 de 42** |
| Forks de `Monkipnet/Tvstreamer_sat` | 3: `juesus/`, `cortijo/`, `sohag1192/` — ninguno tuyo |
| Forks de `Monkipnet/TVStreamer5` | 0 |
| `upstream/main` vs mi clon | `f9af8f6` = `f9af8f6` → **0 commits nuevos** (`rev-list --left-right` = `0 0`) |

Conclusión: **no hay ningún fork tuyo que yo pueda ver y actualizar**, y el original no ha recibido commits nuevos desde la clonación de esta mañana (sigue en `f9af8f6`, `pushed_at` 2026-09-27). El clon de `/home/user/Tvstreamer_sat` ya está en el último estado del upstream; sólo falta apuntar `origin` a tu fork cuando exista:

```bash
cd /home/user/Tvstreamer_sat
git remote rename origin upstream     # deja el original como 'upstream'
git remote add origin git@github.com:TU_USUARIO/Tvstreamer_sat.git
git push -u origin main               # sube main + crea el remote
```

---

## 2. Qué es `TVStreamer5` y cómo se relaciona con `Tvstreamer_sat`

Es el **predecesor** del proyecto que revisamos antes, del mismo autor y con el mismo esqueleto de código, pero **sin la parte de satélite**:

| | `TVStreamer5` (jun–ago 2026) | `Tvstreamer_sat` (sep 2026 →) |
|---|---|---|
| Producto | `TVStreamer` / "TVStreamer5" | `TVStreammerSAT5` |
| Versión | v1 … v115 | 202.xx … 203.75 |
| LOC C++ en `src` | 6 425 (rama `agent`) → **14 978** (v115) | **42 454** |
| Archivos en `src` | 21 (`agent`) → 105 (v115) | 129 |
| DVB-S/S2 | **No** | Sí (`DvbSatellite.cpp`, 1 133 líneas) |
| CA / OSCam / newcamd | **No** | Sí (`CaBackend`, `CardManager`, `OscamMiniManager`, plugin newcamd) |
| MPTS, preview en navegador, HLS scheduler | **No** | Sí |
| Protocolos | mismos módulos `protocols/inputs`, `protocols/outputs`, `stream/*` | idénticos, ampliados |
| Fechas de creación de ambos repos | `2026-09-25 11:01:51Z` | `2026-09-25 11:02:36Z` |

Los dos repos se crearon con **45 segundos de diferencia**: TVStreamer5 quedó como histórico y `Tvstreamer_sat` como línea viva (aquí sí hay satélite + CA). No comparten objetos Git (blobs distintos), pero la arquitectura y el estilo son los mismos: es una migración/reescritura, no un fork de GitHub.

---

## 3. Las 3 ramas: qué hay realmente en cada una

La historia es **lineal**: `codespace-…` ⊂ `agent-…` ⊂ `main`.

| Rama | Último commit | Commits | Estado del árbol | LOC `src` |
|---|---|---|---|---|
| `main` (default) | `06ae075` · 2026-09-28 · "Fresh start" | **254** | **VACÍO**: sólo un `README.md` de 14 bytes con el texto `# TVStreamer5` | 0 |
| `agent-multi-output-stream-formats` | `b58588f` · 2026-08-03 | 124 | Código completo de la época (21 archivos en `src`, + APK de 3,4 MB) | 6 425 |
| `codespace-fantastic-goggles-967jx959wvjgh9rwg` | `a17cc99` · 2026-07-14 | 97 | Versión anterior a la refactorización a `protocols/` | 4 751 |

### 3.1 `main` está en blanco a propósito
`06ae075 "Fresh start"` (2026-09-28) borra **118 archivos / 16 358 líneas**: `src/` completa, `CMakeLists.txt`, `Dockerfile`, `install_deps.sh`, `scripts/`, `docs/screenshots/` y `TVSTREAMER5_V3.apk` (3,4 MB). Deja únicamente `README.md`.

**Nada se ha perdido**: el último estado real de código sigue accesible en `2167f64` (v115, 2026-08-12), que es el padre de `06ae075`. Pero quien entre al repo y pulse "Code → Download ZIP" **se lleva un README de 14 bytes**. Ese es el problema: el estado por defecto del repo está vacío y sin ningún tag ni release que explique dónde está el código.

### 3.2 `agent-multi-output-stream-formats`
Rama residual de un agente (nombre autogenerado, formato `agent-<tarea>`), disparada desde Codespaces. Contiene el código de **inicio de agosto**, con el `CMakeLists.txt` original (proyecto `TVStreamer`, binario `TVStreamer`, Boost `system` + `thread`) y los módulos de la época: `UdpVbrOutput`, `UdpCbrOutput`, `UdpTsOutput`, `WisiCbrOutput` (pacing WISI), `StableUdpOutput`, `MpegTsServiceDetector`.

Está **124 commits por detrás** del último código (v115) y ~8 500 líneas por detrás en `src`. Es la rama más "usable" si alguien quiere código, y aun así es una versión de agosto.

### 3.3 `codespace-fantastic-goggles-967jx959wvjgh9rwg`
Rama de un Codespace con nombre aleatorio. 97 commits, 14 de julio. Es ancestro directo de la anterior: `codespace → agent` son 20 archivos, +2 145/−372. Superada por todo lo demás; es basura de herramienta, no una línea de trabajo.

---

## 4. Hallazgos (por prioridad)

| # | Severidad | Hallazgo |
|---|---|---|
| B1 | **Alta (higiene)** | **La rama por defecto del repo está vacía** y no hay tags ni releases. El código sólo vive en el penúltimo commit (`2167f64`) y en dos ramas obsoletas. Un `git clone` normal no da código; un `zip` tampoco. Si el autor decide hacer `git gc`/rehistoriar o borrar ramas, el proyecto se vuelve inencontrable desde la UI. |
| B2 | Media | **Ramas con nombre de herramienta** (`codespace-fantastic-goggles-…`, `agent-multi-output-stream-formats`): deberían desaparecer o renombrarse a algo con significado (`legacy/v115`, `experiment/multi-output`). Como están, parecen ramas de trabajo activas y confunden a cualquiera que llegue al repo. |
| B3 | Media | **3,4 MB de APK (`TVSTREAMER5_V3.apk`) en el historial** de la rama `agent` y de toda la línea de `main`. Infla el `.git` (4,3 MB total) y es un binario que no se puede revisar. Mejor `releases/` o borrado del historial. |
| B4 | Media | **Sin LICENSE, sin CI, sin tests, sin `.github/`**: misma deuda que `Tvstreamer_sat`, heredada. Cero automatización en 254 commits. |
| B5 | Media | **Misma postura de seguridad**: `admin`/`admin` por defecto (`ConfigManager.h:57-58` en v115), HTTP plano, `filtering_enabled = false` por defecto (`ConfigManager.h:83`), y `/health` + enlaces HTTP-TS/HLS sin auth. El README de la rama `agent` lo admite literalmente: *"HTTP TS and HLS player links are also unauthenticated; enable subscriber filtering when HTTP TS access must be restricted by client IP"*. |
| B6 | Baja | **README desactualizado y sin aviso de estado**: 34 KB en v115, 14 KB en `agent`, ninguno dice "proyecto reemplazado por `Tvstreamer_sat`". Un visitante no puede saber cuál es la línea viva. |
| B7 | Baja | **Mensajes de commit = changelog**: `v111`, `v112`, … `v115` en el asunto, sin `CHANGELOG.md`. Funciona con `git log`, no sirve para releases. |
| B8 | Info | Autoría: 253 commits `monkipnet-spec`, 1 `Monkipnet` (el "Fresh start"). Jun–ago 2026: 1 commit en junio, 96 en julio, 156 en agosto, 1 en septiembre. Desarrollo muy intensivo y luego abandonado. |

---

## 5. Qué haría con esas ramas (concreto)

```bash
cd /home/user/TVStreamer5

# 1) Congelar el último estado bueno en un tag ANOTADO (no cuesta nada y evita perderlo)
git tag -a v115-final 2167f64 -m "Último estado con código antes de 'Fresh start'"
git push origin v115-final

# 2) Restaurar el árbol en main, o al menos un README que apunte al tag
git checkout main
git checkout 2167f64 -- .            # recupera los 118 archivos
git commit -m "Restore v115 tree; archive tag v115-final"

# 3) Archivar/eliminar las ramas de herramienta (el contenido ya está en main)
git push origin --delete codespace-fantastic-goggles-967jx959wvjgh9rwg
git push origin --delete agent-multi-output-stream-formats
#    si quieres conservar la de agosto como referencia:
#    git branch -m agent-multi-output-stream-formats legacy/aug-2026
#    git push origin legacy/aug-2026 && git push origin --delete agent-multi-output-stream-formats

# 4) Sacar el APK del historial (opcional; requiere reescribir)
#    git filter-repo --path TVSTREAMER5_V3.apk --invert-paths
#    (o, más simple: dejarlo y subirlo como Release asset)
```

Y en el `README` de `main`, dos líneas:

```markdown
> **Estado:** proyecto archivado. La línea activa (DVB-S/S2 + CA/OSCam) es
> [`Monkipnet/Tvstreamer_sat`](https://github.com/Monkipnet/Tvstreamer_sat).
> Código de esta versión: tag `v115-final`.
```

---

## 6. Comparativa rápida de los dos repos (para decidir con qué te quedas)

| Criterio | `TVStreamer5` | `Tvstreamer_sat` |
|---|---|---|
| Código disponible en la rama por defecto | **No** (README de 14 bytes) | Sí (`main`) |
| LOC C++ | 14 978 (v115) | 42 454 |
| DVB-S/S2 | No | **Sí** |
| CA / OSCam / newcamd | No | **Sí** |
| MPTS, preview navegador, HLS scheduler | No | **Sí** |
| Ramas | 3 (1 vacía, 2 obsoletas) | 2 (`main` + `backup-v159-20260914`) |
| Tags / Releases / CI | 0 / 0 / 0 | 0 / 0 / 0 |
| LICENSE | No | No |
| Tests | No | 11 `.js` (3 fallan) + 3 `.cpp` no compilados |
| Último commit con código | 2026-08-12 (`2167f64`) | 2026-09-27 (`f9af8f6`) |

**Resumen:** `TVStreamer5` es la generación anterior y útil sólo como referencia histórica (y como `v115-final` congelado). Para trabajar y para el fork, el repo correcto es `Tvstreamer_sat` — que además arrastra los mismos problemas de higiene y de seguridad descritos en `REVISION_Tvstreamer_sat.md`.
