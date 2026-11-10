# Security model y despliegue seguro

Este documento describe **qué protege** TVStreammerSAT5, **qué no protege** y cómo
exponerlo sin regalar el servidor. Aplica a partir de la versión **203.76**.

## 1. Modelo de amenaza

TVStreammerSAT5 es un proceso con privilegios que:

- abre frontends DVB (`/dev/dvb/*`), tarjetas PC/SC y puertos serie;
- lanza GStreamer, `systemctl` y `oscam-mini` como procesos hijos;
- sirve el panel de control y los streams por HTTP;
- reinicia el servicio bajo demanda desde el panel.

Comprometer el panel equivale a comprometer la máquina (el proceso corre como
root por diseño). Por eso el objetivo de seguridad es: **credenciales fuertes,
acceso restringido, y ninguna vía de ejecución de comandos desde la red**.

Fuera de alcance: aislamiento entre streams, multi-tenant, y cualquier defensa
contra alguien que ya tenga root en el host.

## 2. Qué cambia en 203.76

| Antes | Ahora |
|---|---|
| Credenciales `admin`/`admin` de fábrica | **Contraseña aleatoria de 20 caracteres generada al primer arranque** e impresa una sola vez en el log del servicio |
| `POST` de estado sin validación de origen | **Política de mismo origen** para todo lo que no sea `GET`/`HEAD` (`src/HttpOriginPolicy.h`) |
| Sin límite de intentos | **Bloqueo tras 8 fallos** por IP, con ventana de 15 min y castigo de 5 min (HTTP 429) |
| Cuerpo de petición de 512 MiB en todas las rutas | **64 MiB por defecto**, configurable (`max_request_body_mb`, 8–512) y con respuesta `413` |
| Escucha en `0.0.0.0` sin alternativa | `bind_address` para limitar a `127.0.0.1` (proxy TLS inverso) o a una IP concreta |
| URLs de player siempre públicas | `require_stream_auth` exige credenciales también en HTTP-TS/HLS |
| Comandos a `systemctl`/`journalctl`/`ps`/`ss` vía `popen` (shell) | **`posix_spawnp` con argv**, sin shell intermedio |
| Login escrito en el log | Sólo avisos de seguridad (nunca el usuario ni la contraseña) |
| Sin cabeceras de seguridad | `X-Content-Type-Options`, `X-Frame-Options`, `Referrer-Policy`, `Permissions-Policy` |

## 3. Lo que sigue siendo cierto (léelo)

- **No hay TLS.** El panel habla HTTP plano y Basic auth viaja en claro. En
  cualquier red que no sea de confianza, ponlo detrás de un proxy TLS
  (ver `docs/INSTALL_UBUNTU.md`). Alternativa rápida: `bind_address = 127.0.0.1`
  y publicar sólo el proxy.
- **Los streams son públicos por defecto.** Con `require_stream_auth = false` y el
  filtrado de abonados desactivado, quien conozca la URL recibe el canal. Ese es
  el comportamiento histórico, mantenido para no romper emisiones existentes.
- **Los secretos de terceros no están cifrados** en `tvstreammersat5-config.json`:
  token de Telegram, usuarios/claves newcamd y la configuración de OSCam. Sólo la
  contraseña del panel se guarda con AES-256-GCM.
- **La clave de cifrado vive junto al secreto** (`tvstreammersat5-ui.key`, modo
  0600). Protege ante una copia suelta del JSON, no ante un host comprometido.
- **La subida de ficheros de reemplazo** acepta contenido arbitrario que luego
  alimenta GStreamer. Sube sólo ficheros que hayas producido tú.
- El plugin `newcamd` y `third_party/oscam-mini` implementan protocolos de acceso
  condicional: úsalos **sólo con tarjetas y servicios propios o autorizados**.
- **El bloqueo por intentos usa la IP de la conexión**, no `X-Forwarded-For` (no
  se confía en cabeceras que el cliente puede falsificar). Detrás de un proxy
  inverso todas las peticiones llegan desde `127.0.0.1`: 8 fallos de una sola
  persona bloquean el panel 5 minutos para todos. En ese escenario, autentica en
  el proxy (mTLS, allowlist de IP, `auth_request`) además del panel.

## 4. Checklist de exposición

```ini
# tvstreammersat5-config.json
{
  "login": "admin",
  "password": "enc:v1:....",      # cámbiala desde el panel
  "http_port": 9000,
  "bind_address": "127.0.0.1",    # el proxy TLS entra por loopback
  "require_stream_auth": true,     # si los players no son de confianza
  "max_request_body_mb": 64
}
```

```bash
sudo ufw default deny incoming
sudo ufw allow 22/tcp
sudo ufw allow 443/tcp            # proxy TLS público
# 9000 NO se abre a Internet
```

Y en el proxy: TLS con certificado real, `X-Forwarded-For` opcional, y límite de
tamaño de cuerpo si el proxy lo permite.

## 5. Reportar un problema

- Vulnerabilidades del **código original**: `monkipnet@gmail.com`
  (según `src/AppVersion.h`).
- Problemas introducidos en esta copia: abre un *issue* en este repositorio.

No publiques exploits funcionales contra instalaciones de terceros.
