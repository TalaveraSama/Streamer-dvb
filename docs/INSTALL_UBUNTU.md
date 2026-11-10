# Instalación en Ubuntu 22.04 y 24.04

Guía de despliegue del panel TVStreammerSAT5 (203.76) en las dos versiones LTS
soportadas. Probado en CI sobre `ubuntu-22.04` y `ubuntu-24.04`.

## 1. Diferencias entre las dos LTS

| | Ubuntu 22.04 (jammy) | Ubuntu 24.04 (noble) |
|---|---|---|
| Boost | 1.74 → `libboost-system-dev` | 1.83 → `libboost-system-dev` |
| OpenSSL | 3.0 (`libssl-dev`) | 3.0.13 (`libssl-dev`) |
| curl | `libcurl4-openssl-dev` | `libcurl4-openssl-dev` (runtime `libcurl4t64`) |
| jsoncpp | 1.9.5 | 1.9.5 |
| GStreamer | 1.20 | 1.24 |
| Compilador | GCC 11 | GCC 13 |
| `nvh264enc` (NVENC) | requiere driver propietario + `gstreamer1.0-plugins-bad` | igual |

El código compila igual en ambas. `install_deps.sh` resuelve la lista de paquetes
contra la release detectada y avisa si algo no existe en ese catálogo.

## 2. Instalación nativa

### Vía rápida (recomendada en un VPS)

```bash
git clone https://github.com/TalaveraSama/Streamer-dvb.git
cd Streamer-dvb
sudo ./scripts/install_from_source.sh
```

El script instala dependencias, compila, ejecuta `ctest` y la suite de regresión,
copia binario/plugin/OSCam-mini, instala el unit systemd endurecido y arranca el
servicio. Es idempotente (sirve para actualizar) y **no toca** la configuración
existente. Opciones: `--skip-tests`, `--no-start`, `--prefix <ruta>`.

Al terminar:

```bash
journalctl -u tvstreammersat5 -n 30 --no-pager   # aquí sale la contraseña inicial
```

### Paso a paso

```bash
# 1. Dependencias (detecta 22.04/24.04 automáticamente)
sudo ./install_deps.sh

# 2. Verifica el entorno sin instalar nada (útil antes de abrir un issue)
./install_deps.sh --check

# 3. Compila
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc)"

# 4. Tests
ctest --test-dir build --output-on-failure
node tests/run-all.js

# 5. Instala el binario
sudo install -Dm755 build/TVStreammerSAT5 /opt/TVStreammerSAT5/TVStreammerSAT5
sudo install -Dm755 build/tvstreammersat5-ca-newcamd.so \
     /opt/tvstreammersat5/ca-plugins/tvstreammersat5-ca-newcamd.so

# 6. Servicio systemd con hardening (ver SECURITY.md)
sudo install -Dm644 packaging/systemd/tvstreammersat5.service \
     /etc/systemd/system/tvstreammersat5.service
sudo systemctl daemon-reload
sudo systemctl enable --now tvstreammersat5
```

> Si ya usabas `install_tvstreammersat5_binary.sh`, éste conserva el unit file que
> exista en `/etc/systemd/system/tvstreammersat5.service`; el nuevo unit de
> `packaging/systemd/` no se instala automáticamente para no romper despliegues.

### Primer arranque: la contraseña se genera sola

Desde 203.76 **no existe `admin/admin`**. En el primer arranque el panel crea
`tvstreammersat5-config.json` con una contraseña aleatoria de 20 caracteres y la
imprime una única vez:

```bash
sudo journalctl -u tvstreammersat5 -n 30 --no-pager
# ======================================================================
# TVStreammerSAT5 first start: generated credentials
#   login:    admin
#   password: 7fKpQ2mXrT9vLbHs4WnZ
# Store it now and change it in the panel. It is not shown again.
# ======================================================================
```

Guárdala y cámbiala desde **Ajustes**. Si la pierdes:

```bash
sudo systemctl stop tvstreammersat5
sudo rm /opt/TVStreammerSAT5/tvstreammersat5-config.json \
      /opt/TVStreammerSAT5/tvstreammersat5-ui.key
sudo systemctl start tvstreammersat5   # nueva contraseña en el log
```

(Esto borra también la configuración de streams: haz copia antes.)

## 3. Publicarlo sin regalar el servidor

El panel es HTTP plano con Basic auth. En producción:

```
Internet ──TLS──> nginx/Caddy (443) ──HTTP──> 127.0.0.1:9000 ──> TVStreammerSAT5
```

En `tvstreammersat5-config.json` (o Ajustes → Red):

```json
{ "bind_address": "127.0.0.1", "require_stream_auth": true }
```

### Caddy (lo más corto)

```caddyfile
panel.midominio.com {
    encode gzip
    reverse_proxy 127.0.0.1:9000 {
        flush_interval -1          # necesario para HLS/TS en vivo
    }
}
```

### nginx

```nginx
server {
    listen 443 ssl http2;
    server_name panel.midominio.com;

    ssl_certificate     /etc/letsencrypt/live/panel.midominio.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/panel.midominio.com/privkey.pem;

    client_max_body_size 128m;      # subida de ficheros de reemplazo

    location / {
        proxy_pass http://127.0.0.1:9000;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_buffering off;        # no bufferizar MPEG-TS
        proxy_read_timeout 1h;
    }
}
```

> `proxy_set_header Host $host` importa: la política de mismo origen de 203.76
> compara `Origin` con `Host`. Si tu proxy reescribe el Host sin conservar el
> nombre público, los `POST` del panel se rechazarán con 403.

### Cortafuegos

```bash
sudo ufw default deny incoming
sudo ufw allow 22/tcp
sudo ufw allow 443/tcp
sudo ufw enable
```

No abras `9000/tcp` a Internet.

## 4. DVB y tarjetas

```bash
# El servicio corre como root, así que normalmente no hace falta nada más.
ls -l /dev/dvb/adapter0/frontend0

# Lectores USB/serie para OSCam-mini
ls -l /dev/ttyUSB* /dev/ttyACM*
systemctl status pcscd      # para lectores PC/SC
```

Para NVENC:

```bash
nvidia-smi
gst-inspect-1.0 nvh264enc | head -3
```

Si `nvh264enc` no aparece, instala `gstreamer1.0-plugins-bad` y el driver
propietario; el transcoder cae a `x264enc` cuando el modo es *Auto*.

## 5. Contenedor (sólo 24.04)

El `Dockerfile` está fijado a `ubuntu:24.04` porque los nombres de paquetes
runtime cambiaron (`libcurl4t64`, `libssl3t64`, Boost 1.83). Para 22.04 usa la
instalación nativa o crea una variante del Dockerfile con los nombres `t64`
sustituidos.

```bash
docker build -t tvstreammersat5 .
docker run -d --name tvstreammersat5 --network host \
  -v /opt/TVStreammerSAT5:/data \
  -v /dev/dvb:/dev/dvb \
  tvstreammersat5
```

`--network host` es lo habitual porque el panel y las salidas UDP/SRT necesitan
puertos que el operador configura libremente.

## 6. Diagnóstico rápido

| Síntoma | Causa habitual |
|---|---|
| `403 Cross-site request rejected` en el panel | El proxy no conserva `Host` → revisa `proxy_set_header Host` |
| `413 Request body too large` | Subida mayor que `max_request_body_mb` (sube el valor o el `client_max_body_size`) |
| `429 Too many failed authentication attempts` | 8 fallos desde esa IP; espera 5 min o reinicia el servicio |
| `Failed to bind port 9000` | Otro proceso lo ocupa (`ss -ltnp | grep 9000`) |
| Panel inaccesible desde la LAN | `bind_address` está en `127.0.0.1` y el proxy no está delante |
| Pantalla negra en el player y nada en logs | Revisa `journalctl -u tvstreammersat5 -f` y el estado del stream en `/api/state` |

## 7. Actualizar

```bash
cd /ruta/al/repo
git pull
cmake --build build --parallel "$(nproc)"
sudo systemctl stop tvstreammersat5
sudo install -Dm755 build/TVStreammerSAT5 /opt/TVStreammerSAT5/TVStreammerSAT5
sudo install -Dm755 build/tvstreammersat5-ca-newcamd.so \
     /opt/tvstreammersat5/ca-plugins/tvstreammersat5-ca-newcamd.so
sudo systemctl start tvstreammersat5
```

La configuración existente se migra en el arranque: las claves nuevas
(`bind_address`, `require_stream_auth`, `max_request_body_mb`) se añaden al
guardar desde el panel, con sus valores por defecto compatibles.
