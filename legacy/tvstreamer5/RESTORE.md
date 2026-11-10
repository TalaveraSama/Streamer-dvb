# TVStreamer5 — árbol restaurado y snapshots de ramas

Este directorio **recupera el código que desapareció de la rama por defecto de
`Monkipnet/TVStreamer5`**.

## Qué pasó en el repo original

| Rama | Commit | Fecha | Estado del árbol |
|---|---|---|---|
| `main` (default) | `06ae075` "Fresh start" | 2026-09-28 | **Vacío**: sólo `README.md` de 14 bytes. Ese commit borró 118 archivos / 16 358 líneas. |
| `main` (padre, último con código) | `2167f64` "v115" | 2026-08-12 | Código completo → restaurado en **`v115/`** |
| `agent-multi-output-stream-formats` | `b58588f` | 2026-08-03 | Código de agosto → **`snapshots/agent-2026-08-03/`** |
| `codespace-fantastic-goggles-967jx959wvjgh9rwg` | `a17cc99` | 2026-07-14 | Código de julio → **`snapshots/codespace-2026-07-14/`** |

La historia es lineal: `codespace` ⊂ `agent` ⊂ `main`. Por eso `v115/` contiene
todo lo de las otras dos ramas **más** 129 commits posteriores (refactor a
`src/protocols/`, `MpegTsServiceDetector`, reservorios A/V, `WisiCbrOutput`,
etcétera). Los snapshots se incluyen por fidelidad: son el estado exacto de cada
rama en su último commit.

## Contenido

```
legacy/tvstreamer5/
├── v115/                              # git archive 2167f64  (v115, 2026-08-12)  · 117 archivos
├── snapshots/
│   ├── agent-2026-08-03/              # git archive b58588f                       ·  29 archivos
│   └── codespace-2026-07-14/          # git archive a17cc99                       ·  29 archivos
├── export-branch.sh                   # exporta cualquier rama/commit del upstream
└── RESTORE.md                         # este archivo
```

Se han omitido **a propósito** los `TVSTREAMER5_V3.apk` (3,4 MB cada uno) en los
tres árboles: son binarios, no se pueden revisar y engordan el repo. Siguen
disponibles en el historial de `Monkipnet/TVStreamer5`:

```bash
git clone https://github.com/Monkipnet/TVStreamer5.git
git -C TVStreamer5 show 2167f64:TVSTREAMER5_V3.apk > TVSTREAMER5_V3.apk
```

## Verificación de la restauración

El contenido de `v115/` se generó con `git archive 2167f64` directamente desde el
clon del upstream (`/home/user/TVStreamer5`), sin ediciones. Para comprobar que
coincide con el original, excluyendo el APK:

```bash
git clone https://github.com/Monkipnet/TVStreamer5.git /tmp/tv5
cd /tmp/tv5 && git archive 2167f64 | tar -x -C /tmp/tv5-v115
diff -r --exclude='TVSTREAMER5_V3.apk' /tmp/tv5-v115 legacy/tvstreamer5/v115
```

## Regenerar / actualizar un snapshot

```bash
./legacy/tvstreamer5/export-branch.sh v115 2167f64
./legacy/tvstreamer5/export-branch.sh codespace codespace-fantastic-goggles-967jx959wvjgh9rwg
```

El script clona (o reutiliza) el upstream, hace `git archive` del ref indicado y
lo extrae en `legacy/tvstreamer5/<destino>/`, omitiendo el APK.

## Tag

Este repositorio lleva un tag anotado **`tvstreamer5-v115-final`** sobre el commit
que añade `v115/`, para que el estado restaurado tenga una referencia inmutable
aunque el directorio se reorganice:

```bash
git tag -n99 tvstreamer5-v115-final
git checkout tvstreamer5-v115-final -- legacy/tvstreamer5/v115
```

## Qué debería hacer el autor original (recomendación)

En `Monkipnet/TVStreamer5`:

```bash
git tag -a v115-final 2167f64 -m "Último estado con código antes de 'Fresh start'"
git push origin v115-final
git checkout main && git checkout 2167f64 -- .
git commit -m "Restore v115 tree"
git push origin --delete codespace-fantastic-goggles-967jx959wvjgh9rwg
git push origin --delete agent-multi-output-stream-formats
```

Y en el `README.md` de `main`, indicar que la línea activa es
[`Monkipnet/Tvstreamer_sat`](https://github.com/Monkipnet/Tvstreamer_sat).
Mientras eso no pase, este directorio es la única copia del código de TVStreamer5
en un lugar navegable.
