# Revisión técnica — `Monkipnet/Tvstreamer_sat` (TVStreammerSAT5)

**Fecha de revisión:** 2026-11-10
**Commit revisado:** `f9af8f6` (`main`, HEAD; "Remove obsolete patch files and old APK")
**Rama revisada además:** `backup-v159-20260914`
**Clon local:** `/home/user/Tvstreamer_sat`
**Volumen:** ~574 commits, 1 autor real (`monkipnet-spec`), 42 454 líneas de C/C++ + 788 KB de web + 6 MB de OSCam-mini vendorizado.

---

## 1. Resumen ejecutivo

TVStreammerSAT5 es un **servidor de recepción y distribución de TV (DVB-S/S2 y red) escrito en C++17 sobre GStreamer**, con panel web propio, transcodificación opcional (x264 / NVENC), salidas UDP/RTP/SRT/HTTP/HLS/RTSP/RTMP y soporte de acceso condicional (CA) mediante plugins y OSCam-mini.

Es un proyecto **funcional, ambicioso y con ingeniería real detrás** (control de PCR/CC, CBR pacing, watchdog de media, arbitraje de frontends DVB, MPTS, perfiles SRT…), pero con **deuda técnica estructural** (dos archivos concentran ~21 000 líneas, con la UI entera embebida como string en C++), **sin licencia**, **sin CI**, y con un **modelo de seguridad de red débil para un servicio que se expone en LAN/Internet**.

Veredicto rápido:

| Área | Nota | Comentario |
|---|---|---|
| Idea / dominio técnico | ★★★★☆ | Muy completo en protocolos y en detalles de MPEG-TS/DVB |
| Arquitectura | ★★★☆☆ | Capa de protocolos bien separada; el "core" no |
| Calidad / mantenibilidad | ★★☆☆☆ | God-files, UI en C++, sin capa de tests C++ real |
| Seguridad | ★★☆☆☆ | HTTP plano + Basic auth, endpoints de estado mutables sin CSRF, `system()`/`popen()` |
| Reproducibilidad / build | ★★★★☆ | CMake correcto, Dockerfile con verificación de plugins GStreamer |
| Higiene de repo | ★★☆☆☆ | Sin LICENSE, sin CI, binarios y `__pycache__` versionados |
| Tests | ★☆☆☆☆ | 10 scripts de "grep-test" JS; **3 fallan en HEAD**; los .cpp no están en CMake |

---

## 2. Ficha técnica

| Dato | Valor |
|---|---|
| Lenguaje | C++17 (`CMakeLists.txt:4`), JavaScript embebido en C++ |
| Versión declarada | `203.75` (`src/AppVersion.h`), README desactualizado en `203.73` |
| Dependencias | GStreamer 1.0 (base/app/mpegts), Boost (thread, Beast, Asio), OpenSSL, libcurl, jsoncpp, libdvbcsa, libpcsclite |
| Binarios producidos | `TVStreammerSAT5`, `tvstreammersat5-ca-newcamd.so`, `oscam-mini` |
| Puerto panel | `9000/tcp` (`ConfigManager.h:126`) |
| Credenciales iniciales | `admin` / `admin` (`ConfigManager.h:124-125`) |
| Empaquetado | Dockerfile multi-stage (Ubuntu 24.04), scripts install/uninstall, systemd en `packaging/` |
| Licencia | **Ninguna** (no hay `LICENSE`/`COPYING` en la raíz) |
| CI | **Ninguna** (no existe `.github/`) |
| Idiomas de UI | Ruso + inglés (i18n por tablas en JS) |

---

## 3. Arquitectura

El diseño declarado en el README se cumple en la parte de protocolos:

```
src/protocols/inputs/          <- módulos de entrada del camino transcoder (GStreamer)
src/protocols/outputs/         <- módulos de salida del camino transcoder
src/protocols/stream/inputs/   <- entradas del camino passthrough/remux
src/protocols/stream/outputs/  <- salidas del camino passthrough/remux
```

Cada protocolo es una clase con interfaz común (`GstInputProtocols.h`, `StreamInputProtocol.h`, etc.). Esto es **lo mejor del repo**: añadir un protocolo nuevo es mecánico y no toca el core. `CMakeLists.txt` lista cada `.cpp` explícitamente (sin globs), lo que es correcto y explícito, al precio de tener que editar el build en cada archivo nuevo.

El problema es el núcleo:

| Archivo | Líneas | Responsabilidad |
|---|---|---|
| `src/StreamManager.cpp` | **13 483** | ciclo de vida de pipelines, UDP/SRT, remux, PID remap, CBR, MPTS, watchdogs, reinicios |
| `src/HttpServer.cpp` | **7 333** | HTTP + router + **toda la UI web (HTML/CSS/JS/i18n) como literal C++** |
| `src/DvbSatellite.cpp` | 1 133 | frontend DVB, escaneo PAT/PMT/SDT, LOCK |
| `src/CaBackend.cpp` | 614 | carga de plugins CA (ABI C v1) |
| `src/ConfigManager.cpp` | 758 | JSON + AES-256-GCM del password |

Consecuencias concretas:
- Un cambio de texto en la UI (p. ej. una cadena en ruso) implica recompilar C++ y reemitir el binario.
- El `HttpServer` mezcla I/O de red, lógica de negocio y presentación; no hay separación entre capa HTTP y handlers.
- El estado global (`configManager`, `streamManager`, mutex de calidad, memo de CA decode) se toca desde hilos de sesión distintos (`kHttpSessionWorkers = 24`), con varios `std::thread(...).detach()` (7 en total). Es fácil introducir carreras al añadir features.

---

## 4. Lo que está bien

1. **Cobertura de protocolos real y poco común**: UDP unicast/multicast, RTP, SRT caller/listener, HLS (con access key por header o query), RTSP in/out, RTMP push, HTTP-TS, fichero en bucle, test bars, DVB-S/S2.
2. **Detalles de MPEG-TS cuidados**: `TsCcStageTrace.h` (trazado de continuity counter por etapa), control de PCR, CBR pacing (`UdpCbrOutput.cpp`, `NetupOutputPacer.cpp`), `StableUdpOutput.cpp`, `UdpMediaDeliveryHealth.h`.
3. **Watchdogs con nombre y documentación por release**: `docs/MEDIA_WATCHDOG_20369.md`, `UDP_MEDIA_WATCHDOG_20370.md`, y reinicio automático del servicio ante teardown colgado (`StreamManager.cpp:503-545`), con `systemctl --no-block` para no autobloquearse. Es un caso clásico resuelto conscientemente.
4. **Ciclo de vida de pipelines con weak-ref de GObject** antes de `unref` (`StreamManager.cpp:538+`): demuestra que el autor ha peleado con fugas reales de GStreamer, no es código de tutorial.
5. **Comparación de credenciales en tiempo constante** (`HttpServer.cpp:516-526`), y password cifrado en disco con AES-256-GCM + clave local `0600` (`ConfigManager.cpp:88,123,155-235`).
6. **Dockerfile con verificación activa**: tras instalar, hace `gst-inspect-1.0` de ~25 elementos y falla el build si falta un encoder AAC (`Dockerfile:57-70`). Esto es más de lo que hace la mayoría de proyectos.
7. **Separación limpia del CA en plugins con ABI C estable** (`src/ca/CaBackendPluginApi.h`, `docs/CA_BACKEND_PLUGIN_API.md`): el core no contiene un descifrador; la política de acceso queda en un `.so` externo.
8. **Control de frontend DVB compartido por transpondedor** (evita el error típico de abrir el mismo `/dev/dvb` dos veces).

---

## 5. Hallazgos de seguridad

> Contexto: el panel escucha en `0.0.0.0:9000` por defecto. Todo lo siguiente se agrava si el puerto se publica a Internet, que es el uso habitual de esta categoría de software.

| # | Severidad | Hallazgo | Evidencia |
|---|---|---|---|
| S1 | **Alta** | **Sin TLS.** Basic auth viaja en claro; cualquiera en la ruta lee usuario/clave y puede secuestrar el panel (control total de streams, CA y reinicios del servicio). | `HttpServer.h`: sólo `boost/asio` + `boost/beast/http`, sin `ssl::stream` |
| S2 | **Alta** | **Credenciales por defecto `admin/admin`** y sin forzar cambio ni bloqueo por intentos. | `ConfigManager.h:124-125` |
| S3 | **Alta** | **Streams públicos por defecto**: con `filtering_enabled=false` (valor por defecto) cualquier IP que conozca la URL `/streamX.ts` o la playlist HLS recibe el canal; la autenticación se omite deliberadamente para esas rutas. | `ConfigManager.h:153` (`false`), `HttpServer.cpp:1142-1153` (`requiresAuthentication`), `HttpServer.cpp:1188-1189` (`return true` si el filtrado está off) |
| S4 | Media | **Sin protección CSRF**: los POST de estado (`/api/save-config`, `/api/restart-program`, `/api/dvb-scan`, `/api/oscam-mini/*`) no validan `Origin`/`Referer` ni token. Como el navegador reenvía automáticamente Basic auth cacheada del mismo origen, una web maliciosa puede disparar acciones contra `http://IP:9000`. | `HttpServer.cpp:1058-1120` (rutas), sin comprobación de `Origin` en todo el archivo |
| S5 | Media | **`body_limit` de 512 MB** en el parser HTTP: un POST grande (aunque requiera auth) agota memoria del proceso, que además gestiona cientos de hilos GStreamer. | `HttpServer.cpp:767` |
| S6 | Media | **Escritura de fichero desde el panel**: `/api/upload-backup-file` vuelca el body tal cual a `backup-files/<stream>-<epoch>-<nombre>`. Los nombres se sanean (`cleanFileName`, `HttpServer.cpp:476`), pero el contenido es arbitrario y esos ficheros se usan como **fuente de reemplazo** que alimenta `uridecodebin`/GStreamer; subir un fichero malformado explota el demuxer dentro del proceso. | `HttpServer.cpp:3066-3105`, `HttpServer.cpp:2433` |
| S7 | Media | **`system()` y `popen()`**: `systemctl --no-block restart` (constante, OK), y en `OscamMiniManager::run()` se compone el comando con concatenación de cadenas y se ejecuta vía shell `popen(cmd + " 2>&1")`. Hoy los argumentos vienen de rutas de dispositivo y settings del admin, pero es una inyección de shell esperando un input nuevo. | `OscamMiniManager.cpp:246`, `HttpServer.cpp:3229`, `StreamManager.cpp:522` |
| S8 | Media | **Clave de descifrado junto al secreto**: `tvstreammersat5-ui.key` (0600) vive en el mismo directorio que `tvstreammersat5-config.json`. Quien lea el primer archivo descifra el segundo; el cifrado protege contra backup suelto, no contra compromiso del host. Está documentado con honestidad en el README, pero conviene rotularlo como tal. | `ConfigManager.cpp:694-695` |
| S9 | Baja | **Secretos en claro en el JSON**: token de Telegram, usuarios/claves newcamd y OSCam se serializan en `tvstreammersat5-config.json` / configs de oscam sin cifrar; sólo el password del panel está cifrado. | `ConfigManager.cpp:621-635`, `packaging/oscam-mini/default-config/*` |
| S10 | Baja | **Credenciales por defecto en OSCam-mini**: `user=irdeto_user`, `pwd=change_me`, key newcamd `0102030405060708091011121314` (la key estándar del protocolo, pero conviene validarla al arrancar). | `OscamMiniManager.cpp:363`, `packaging/oscam-mini/default-config/oscam.user` |
| S11 | Baja | **Login impreso en stderr** al arrancar (`main.cpp:51-52`), útil para diagnóstico pero filtra nombres de usuario a los logs/journal. | `main.cpp:51` |
| S12 | Info | `/health` es anónimo a propósito; sirve de oráculo de existencia del servicio. | `HttpServer.cpp:1030,1150` |

Mitigaciones ya presentes que conviene no perder: comparación en tiempo constante, `body_limit` (aunque alto), saneado de nombres en borrado (`safeName != requestedName` → rechazo, `HttpServer.cpp:3096`), resolución de rutas HLS/HTTP-TS restringida a streams configurados (`HttpServer.cpp:337-410`), y el comentario explícito de que el preview nunca es público.

---

## 6. Calidad de código y mantenibilidad

1. **God-files**: `StreamManager.cpp` (13 483 líneas) y `HttpServer.cpp` (7 333) harían inviable un code review serio en un PR. Sugerencia: extraer `HttpRouter`, `PreviewController`, `SubscriberController`, y de `StreamManager` sacar `PipelineFactory`, `PidRemapper`, `WatchdogSupervisor`.
2. **UI web embebida en C++** (RAW string con HTML+CSS+JS+i18n ruso/inglés dentro de `HttpServer.cpp`). Además de la recompilación por cada cambio, impide lint/format/testing de la UI y obliga a reglas de traducción por regex (`['Поиск DVB frontend...', 'Searching DVB frontend...']`, `HttpServer.cpp:3853+`). El código de preview sí está fuera (`web/preview/preview-player.js`) — irónicamente lo moderno quedó fuera y lo grande dentro.
3. **Convención de versiones en comentarios** (`// 202.47:`, `// 202.66:`) por todo el código: buena trazabilidad histórica, mala como documentación viva. En un fork conviene moverlas a `CHANGELOG.md`.
4. **Comentarios de calidad**: los hay excelentes explicando el *por qué* (arena caps de glibc en `main.cpp:17-27`, weak-ref de GStreamer, `--no-block` en systemd). Es el mejor activo del repo para un mantenedor nuevo.
5. **Concurrencia**: 24 workers HTTP + hilos GStreamer + 7 `detach()`. Sin `std::jthread`, sin executor propio, sin tests de concurrencia. Alto riesgo de regresión.
6. **Sin LICENSE** → legalmente es *todos los derechos reservados*. Un fork público es redistribución: hay que pedir permiso al autor o esperar una licencia. Además `third_party/oscam-mini` trae su propio `COPYING` (GPL), que **no se puede relicenciar** ni mezclar alegremente con código propietario si se distribuye el binario.
7. **Binarios versionados**: `ServerManager1.1.apk` (23 KB, un cliente Android mínimo: `classes2.dex` de 36 KB sin comprimir, sin fuentes) y `web/vendor/hls.min.js` (532 KB) + `mpegts.min.js` (216 KB). Los `.min.js` al menos traen `LICENSE.hls.js`/`LICENSE.mpegts.js`.
8. **`scripts/__pycache__` versionado** y `README` con la versión atrasada respecto a `AppVersion.h`.
9. **Documentación**: 11 documentos en `docs/` + 4 capturas. Bien para un proyecto personal; falta un `ARCHITECTURE.md` que describa el flujo de un paquete TS de entrada a salida.

---

## 7. Build, tests y CI

**Build.** `CMakeLists.txt` está bien: C++17, `PkgConfig` para todo, `find_package(OpenSSL)`, detección de `libdvbcsa` con varios nombres de paquete y `FATAL_ERROR` claro si falta. El plugin `tvstreammersat5-ca-newcamd.so` se construye siempre y se instala en `/opt/tvstreammersat5/ca-plugins`; el de ejemplo es opcional (OFF). El Dockerfile verifica los plugins GStreamer en build-time. Puntos menores: `link_libraries(glib-2.0 gio-2.0)` por nombre en vez de vía `pkg-config` (`GSTREAMER_LIBRARIES` ya las incluye), y `CMAKE_CXX_FLAGS` con warnings/`-Werror` ausentes.

No he podido compilar aquí (no hay GStreamer/libdvbcsa/Boost de desarrollo en el sandbox ni acceso a apt), así que **la revisión del build es estática**.

**Tests.** Hay 14 archivos en `tests/`: 11 `.js` y 3 `.cpp`. Los `.js` no son tests de comportamiento: leen el `.cpp`/`.js` y hacen `assert` sobre la **presencia de substrings** (regex de código fuente). Ejecutados con Node 22:

```
8/11 OK
OK   test_browser_preview.js        OK   test_preview_hls_20372.js
OK   test_media_watchdog_20369.js   OK   test_rtsp_hls_direct_20373.js
OK   test_oscam_pcsc_20372.js       OK   test_sd_widescreen_ui_20371.js
OK   test_preview_close_20368.js    OK   test_udp_watchdog_20370.js
FAIL test_english_ui_20373.js
FAIL test_rtsp_input_20373.js
FAIL test_sat_preview_20373.js
```

Los 3 fallos son todos por lo mismo: **los tests fijan la versión literal `203.73`** y `src/AppVersion.h` ya está en `203.75`:

- `test_english_ui_20373.js:7` → `numeric version must stay 203.73`
- `test_rtsp_input_20373.js:9` → `version must remain 203.73`
- `test_sat_preview_20373.js:20` → `assert(version.includes('"203.73"'))`

Es decir, **HEAD está en rojo por diseño**: cada bump de versión rompe 3 tests y hay que editarlos a mano. En un fork, lo primero es sustituir esa aserción por un rango (`>=`) o por un fixture.

Los 3 `.cpp` (`test_sd_widescreen_20371.cpp`, `test_ts_media_progress_20369.cpp`, `test_udp_media_delivery_20370.cpp`) **no están referenciados en `CMakeLists.txt`**: no se compilan ni se ejecutan en ningún flujo automatizado. No hay `ctest`, ni `add_test`, ni framework (GTest/Catch2).

**CI.** No existe `.github/workflows`. Nada impide subir un commit que no compila.

---

## 8. Legal / cumplimiento (importante antes de hacer un fork público)

- **No hay licencia** → por defecto, copyright total del autor. Clonar en local es libre; **publicar un fork o redistribuir binarios requiere autorización** del titular.
- El proyecto incluye **acceso condicional**: `CardManager`, `newcamd` (protocolo de *cardsharing*), `OSCam-mini` vendorizado y el plugin CA. La documentación lo enmarca como *"operator/manufacturer smart-card SDK"* local y "sin servidor de cardsharing en red" (`docs/CA_BACKEND_PLUGIN_API.md:3`), lo cual es la forma legítima de usarlo (tarjeta propia, operador autorizado). Aun así, **la combinación DVB + OSCam + newcamd es la infraestructura típica de la reventa no autorizada de TV de pago**: si el fork va a ser público, conviene dejar explícito en el README que el uso previsto es con tarjetas y servicios propios/autorizados, y revisar la GPL de OSCam si se distribuye el binario.
- Uso de marcas/nombres de operadores o de `oscam` en la UI y docs: sin LICENSE, la exposición es del titular.

---

## 9. Si haces el fork: orden recomendado de trabajo

**Antes que nada (bloqueantes):**
1. Pedir/definir licencia al autor original, y añadir `LICENSE` + aviso de terceros (`third_party/oscam-mini/COPYING`, `web/vendor/LICENSE.*`).
2. Añadir CI mínima: `ubuntu-24.04`, `install_deps.sh` + `cmake --build`, más ejecutar los `.js` de `tests/`; arreglar los 3 tests con versión fijada.
3. Registrar los 3 tests `.cpp` en CMake con `add_test` y `enable_testing()`.

**Seguridad (1-2 días):**
4. Terminar TLS en un reverse proxy documentado (Caddy/nginx con Basic→OIDC) **o** añadir `boost::asio::ssl` opcional.
5. Forzar cambio de `admin/admin` en el primer arranque; *rate limit* y *lockout* en auth.
6. Validar `Origin`/`Referer` en todos los POST de estado (o pasar a sesión con cookie `SameSite=Strict` + token anti-CSRF).
7. Bajar `body_limit` a algo razonable (p. ej. 64 MB) y limitarlo sólo en la ruta de subida.
8. Cifrar también secretos de Telegram/newcamd con la misma clave, o aceptar y documentar que sólo el password del panel está protegido.

**Mantenibilidad:**
9. Sacar la UI web a `web/` con build estático (o al menos a un header generado) y quitar el literal de `HttpServer.cpp`.
10. Partir `StreamManager.cpp` y `HttpServer.cpp` por responsabilidad.
11. `CHANGELOG.md` con el histórico `202.xx`, y `ARCHITECTURE.md` con el flujo TS→salida.

---

## 10. Nota sobre el fork en GitHub

Intentado desde este entorno con la integración de GitHub conectada (`TalaveraSama`), sobre `Monkipnet/Tvstreamer_sat`:

```
gh repo fork Monkipnet/Tvstreamer_sat --clone=false
→ HTTP 403: Resource not accessible by integration (POST /repos/Monkipnet/Tvstreamer_sat/forks)

POST /user/repos (fallback: repositorio espejo)
→ HTTP 403: Resource not accessible by integration
```

El token disponible es un **token de instalación de GitHub App** con permiso sólo sobre repositorios ya existentes (puede hacer `push` en `TalaveraSama/Streamer-dvb`, pero **no puede crear repositorios ni forks**). Por tanto **el fork no se ha creado**: hay que hacerlo desde la web (`https://github.com/Monkipnet/Tvstreamer_sat/fork`) o reconectando GitHub en Arena con un token/instalación que tenga permiso de creación de repositorios.

El clon local sí está completo en `/home/user/Tvstreamer_sat` (rama `main`, `f9af8f6`, con `origin` apuntando al original y la rama `backup-v159-20260914` disponible), listo para apuntarlo a tu fork con:

```bash
cd /home/user/Tvstreamer_sat
git remote rename origin upstream
git remote add origin git@github.com:TalaveraSama/Tvstreamer_sat.git
git push -u origin main
```
