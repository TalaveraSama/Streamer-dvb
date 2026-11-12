# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/).
Las versiones siguen el esquema del proyecto original (`203.xx`).

## En desarrollo

- Importador Flussonic: opción explícita para recibir cada stream por HLS desde
  un servidor local usando su ID, conservando los destinos UDP y el mapeo de interfaz.

- Compatibilidad de compilación para la API de solicitud de pads de GStreamer
  1.16 y 1.20+. Ubuntu 20.04 sigue siendo experimental hasta validar CI y ejecución.
- Con glibc anterior a 2.33, las métricas de malloc no están disponibles y
  aparecen en cero; las demás métricas de memoria siguen funcionando.

- Un solo comando detecta archivos `config.txt` de Astra o Flussonic y prepara
  una copia de revisión; permite aceptar expresamente una conversión parcial.

## [203.78-rc1] - 2026-11-10 — candidata de pruebas

- Vistas previas de migración Astra y Flussonic sin ejecutar entradas ni
  importar usuarios o claves globales. Flussonic conserva destinos UDP tras
  confirmar la interfaz; ambas copias se generan aparte y quedan detenidas.

- Mini-OSCam: tema adaptable con AdminLTE 3.2.0 servido localmente y selector
  inglés/español. Se conserva el motor OSCam existente.
- Navegación principal con opción inicial de español; la traducción del resto
  del panel todavía está incompleta (ver `docs/INTERFAZ_MODERNIZACION.md`).
- Se preservan lectores deshabilitados, `keepalive` y `detect`; los errores de
  guardado ya no descartan el formulario. Archivos temporales de configuración
  con permisos privados desde su creación.
- Los instaladores desde fuente, CMake, Docker y binario incluyen los recursos
  de la interfaz. El instalador universal requiere actualización adicional.

## [203.77] - 2026-11-10

Corrección del instalador para Ubuntu 22.04 detectada por la CI recién añadida.

### Corregido

- **`install_deps.sh` en Ubuntu 22.04**: `libgstreamer1.0-dev` depende de
  `libunwind-dev`, que fija `libunwind8` a la versión del archivo. Las imágenes
  cloud con un `libunwind8` más nuevo hacían que `apt-get install` abortara con
  *"held broken packages"* sin nombrar la causa. Ahora `libunwind-dev` se
  reconcilia explícitamente (con `--allow-downgrades` si hace falta) antes de
  instalar el conjunto requerido.
- **Paquetes opcionales no bloqueantes**: VA-API, `vainfo`, `intel-media-va-driver`,
  `pcscd` y `pcsc-tools` se instalan en una segunda pasada que puede fallar sin
  abortar la instalación.
- `apt-get` con `DPkg::Lock::Timeout=180` para no chocar con `unattended-upgrades`.

### Mejorado

- Diagnóstico de CI: el log de dependencias se guarda como artefacto y los
  errores de apt/dpkg se emiten como anotaciones (`GITHUB_ACTIONS` se pasa a
  través de `sudo`), además de un `apt-get -s` compacto al fallar.

### Verificado

- CI **en verde** en `ubuntu-22.04` y `ubuntu-24.04`: dependencias, compilación,
  4 tests de CTest y 12 tests de regresión Node.

## [203.76] - 2026-11-10

Endurecimiento de seguridad, arreglo de la suite de tests y soporte declarado de
Ubuntu 22.04/24.04 sobre la copia de trabajo de `Monkipnet/Tvstreamer_sat`
(commit `f9af8f6`, versión 203.75).

### Seguridad

- **Credenciales iniciales generadas**: se elimina `admin`/`admin`. En el primer
  arranque se genera una contraseña aleatoria de 20 caracteres (OpenSSL
  `RAND_bytes`, muestreo por rechazo) y se imprime una sola vez en el log del
  servicio. (`src/ConfigManager.cpp`)
- **Política de mismo origen** para peticiones que no son `GET`/`HEAD`: bloquea
  CSRF sobre la Basic auth cacheada por el navegador. Las peticiones sin
  `Origin`/`Referer` (curl, players) no se ven afectadas.
  (`src/HttpOriginPolicy.h`, `src/HttpServer.cpp`)
- **Límite de intentos de autenticación**: 8 fallos por IP en 15 minutos →
  bloqueo de 5 minutos con `429` y `Retry-After`. (`src/HttpServer.cpp`)
- **Cuerpo de petición acotado**: de 512 MiB fijos a 64 MiB por defecto,
  configurable (`max_request_body_mb`, 8–512) y con respuesta `413` en lugar de
  cerrar la conexión. (`src/HttpServer.cpp`, `src/ConfigManager.*`)
- **Dirección de escucha configurable** (`bind_address`), para dejar el panel en
  `127.0.0.1` detrás de un proxy TLS. (`src/HttpServer.cpp`)
- **Autenticación opcional en URLs de player** (`require_stream_auth`), para que
  HTTP-TS y HLS no sean públicos por defecto en instalaciones nuevas que lo activen.
- **Sin shell**: `OscamMiniManager` ejecuta `systemctl`, `journalctl`, `ps` y `ss`
  con `posix_spawnp` y argv explícito en lugar de `popen` con concatenación de
  cadenas. El PID usado en `journalctl` se valida como numérico.
  (`src/OscamMiniManager.cpp`)
- **Cabeceras de seguridad** en todas las respuestas (`X-Content-Type-Options`,
  `X-Frame-Options`, `Referrer-Policy`, `Permissions-Policy`).
- **Logs**: ya no se escribe el nombre de usuario al arrancar; se avisa
  explícitamente de contraseña débil, escucha en todas las interfaces y streams
  públicos. (`src/main.cpp`)
- **Unit systemd endurecido** en `packaging/systemd/tvstreammersat5.service`.

### Añadido

- `tests/test_http_origin_20376.cpp`: 20 casos de la política de mismo origen,
  incluyendo IPv6, userinfo, `Origin: null`, proxy TLS y puertos distintos.
- `tests/run-all.js`: ejecutor único de la suite de regresión (11 tests).
- CI en GitHub Actions con matriz `ubuntu-22.04`/`ubuntu-24.04`: build, `ctest`,
  suite Node, comprobación de sintaxis de shell/JS y guardas de higiene.
  (`.github/workflows/ci.yml`)
- `docs/INSTALL_UBUNTU.md`: instalación nativa en 22.04/24.04, proxy TLS
  (Caddy/nginx), cortafuegos, DVB/NVENC y diagnóstico.
- `SECURITY.md`: modelo de amenaza, cambios de esta versión y lo que **sigue sin
  estar cubierto** (TLS, streams públicos, secretos de terceros en claro).
- `install_deps.sh --check`: verifica pkg-config, cabeceras y elementos de
  GStreamer sin instalar nada.
- `scripts/install_from_source.sh`: instalación/actualización completa en un VPS
  (dependencias, build, tests, binario, plugin CA, OSCam-mini, unit systemd
  endurecido y arranque), idempotente y sin tocar la configuración existente.

### Cambiado

- `install_deps.sh` resuelve los paquetes contra la release detectada, separa
  requeridos de opcionales y falla con un mensaje claro si falta `universe`.
- `CMakeLists.txt`: `enable_testing()` + los 4 tests C++ registrados en CTest
  (antes los 3 existentes no se compilaban nunca), y opción de warnings
  (`-Wall -Wextra`, activada por defecto).
- La versión mostrada en el panel ya no está escrita a mano en el HTML: se inyecta
  desde `src/AppVersion.h` (`window.__TVS_VERSION`). Igual el diagnóstico
  `TVS_STATE_API_DIAG_BYPASS`, que reportaba `202.68`.
- Los tests de regresión leen la versión desde `AppVersion.h` y validan su forma
  en vez de fijar el literal: un cambio de versión ya no rompe 3 tests.
- `README.md`: versión actualizada a 203.76 y aviso de seguridad/instalación.

### Corregido

- Los 3 tests que fallaban en `main` (`test_english_ui_20373.js`,
  `test_rtsp_input_20373.js`, `test_sat_preview_20373.js`) por fijar `203.73`.

### Pendiente (siguiente iteración)

- TLS nativo en el panel (hoy sólo reverse proxy documentado).
- Exponer `bind_address`, `require_stream_auth` y `max_request_body_mb` en la
  pantalla de Ajustes (hoy se editan en `tvstreammersat5-config.json`).
- Cifrado del token de Telegram y de las credenciales newcamd/OSCam en el JSON.
- Verificación de firma en la subida de ficheros de reemplazo.
- Dividir `HttpServer.cpp`/`StreamManager.cpp` y extraer la UI web a `web/`.
- Licencia: el código original no tiene `LICENSE`; ver `NOTICE.md`.
