# NOTICE / Atribución y licencias

## Código de terceros incluido en este repositorio

| Componente | Ruta | Origen | Licencia |
|---|---|---|---|
| TVStreammerSAT5 203.75 | raíz (`src/`, `web/`, …) | `Monkipnet/Tvstreamer_sat` @ `f9af8f6` (2026-09-27) | **Sin licencia** → todos los derechos reservados del autor |
| TVStreamer5 v115 | `legacy/tvstreamer5/v115/` | `Monkipnet/TVStreamer5` @ `2167f64` (2026-08-12) | **Sin licencia** → todos los derechos reservados del autor |
| TVStreamer5 ramas antiguas | `legacy/tvstreamer5/snapshots/*` | `Monkipnet/TVStreamer5` @ `b58588f`, `a17cc99` | **Sin licencia** |
| OSCam-mini (vendorizado) | `third_party/oscam-mini/` | Proyecto OSCam | **GPL** — ver `third_party/oscam-mini/COPYING` |
| hls.js | `web/vendor/hls.min.js` | video-dev/hls.js | Apache-2.0 — ver `web/vendor/LICENSE.hls.js` |
| mpegts.js | `web/vendor/mpegts.min.js` | xqq/mpegts.js | Apache-2.0 — ver `web/vendor/LICENSE.mpegts.js` |
| GStreamer, Boost, OpenSSL, jsoncpp, libcurl, libdvbcsa | enlazados dinámicamente | proyectos respectivos | LGPL/Boost/Apache/MIT según componente; **no** se redistribuyen aquí |

## Qué implica que el código original no tenga licencia

1. Sin `LICENSE`, no se concede ningún derecho más allá del que da la ley: puedes
   clonar, compilar y modificar en local, pero **no** tienes permiso explícito de
   redistribuir ni de relicenciar.
2. Por tanto este repositorio es una **copia de trabajo privada/de uso propio**.
   Si quieres publicarlo o distribuirlo, pide autorización a `Monkipnet`
   (`monkipnet@gmail.com`, según `src/AppVersion.h`) o espera a que publique una
   licencia.
3. No se puede aplicar una licencia nueva al conjunto mientras no haya cesión del
   autor, y **nunca** se puede aplicar una licencia incompatible con la GPL de
   OSCam-mini si se distribuyen binarios que lo incluyan.

## Uso previsto

El componente de acceso condicional (`src/CaBackend*`, `src/CardManager*`,
`src/OscamMiniManager*`, `src/ca/backends/newcamd`, `third_party/oscam-mini`) está
documentado por el autor como interfaz para **tarjetas y operadores propios o
autorizados** (`docs/CA_BACKEND_PLUGIN_API.md`). Úsalo sólo con contenido y
servicios sobre los que tengas derechos.

## Marcas

Los nombres de operadores, plataformas o productos citados en el código o la
documentación pertenecen a sus titulares y se usan aquí sólo como referencia
técnica.
