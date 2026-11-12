# Importador único de `config.txt` (en desarrollo)

`scripts/import_config.py` detecta el **contenido**, independientemente de su
extensión: Astra/Cesbo JSON con `make_stream`, o configuración de texto con
bloques `stream` de Flussonic. Un archivo por ejecución. Es una herramienta de
línea de comandos, **no un botón del panel**, y todavía no está incluida en el
prerrelease `v203.78-rc1` publicado.

Tras instalar una versión que lo incluya con `install_from_source.sh`, el comando
estará en `/opt/TVStreammerSAT5/tools/import_config.py`. La vista previa no
escribe ni ejecuta el archivo:

```bash
python3 /opt/TVStreammerSAT5/tools/import_config.py /root/config.txt \
  /opt/TVStreammerSAT5/tvstreammersat5-config.json
```

Cuando estén revisadas las advertencias, se puede generar una **copia nueva**:

```bash
python3 /opt/TVStreammerSAT5/tools/import_config.py /root/config.txt \
  /opt/TVStreammerSAT5/tvstreammersat5-config.json \
  --output /root/configuracion-para-revisar.json
```

No se sobrescriben archivos, no se modifica el servicio ni se aplican los
cambios. La copia contiene la configuración actual de destino y puede incluir
secretos: se crea con permisos `0600` y debe protegerse. No compartas el archivo
Astra/Flussonic original, contraseñas, tokens ni el archivo resultante.

Los canales convertidos quedan sin arranque automático. El resumen informa de
los casos que no se pudieron convertir. `--details` muestra sus identificadores
y motivos (trata esa salida como privada). Si hay casos pendientes, la copia no se
genera salvo que se autorice expresamente una importación parcial con
`--allow-partial`. Esto **omite** los canales sin convertir: no significa que
estén importados. La confirmación no anula la comprobación de conflictos UDP.

## Parámetros según el formato detectado

**Astra:** para las entradas DVB hay que confirmar adaptadores y valores de LNB
reales con `--confirm-adapters`, `--lof1`, `--lof2` y `--slof`. Si hay DiSEqC,
indicar el mapeo con `--diseqc ID=FUENTE_DESTINO`. Salidas de Astra, CAM,
credenciales y EPG no se importan. Las salidas UDP de los canales importados
son marcadores locales, no destinos de producción. Ver
[IMPORTAR_ASTRA.md](IMPORTAR_ASTRA.md).

**Flussonic:** conserva títulos y destinos UDP del archivo cuando se confirma
la interfaz con `--interface ORIGEN=DESTINO`. Convierte las entradas `m4f` a
HTTP HLS añadiendo `/index.m3u8` al mismo host y ruta; es necesario comprobar
que la fuente realmente funcione. Las URLs con query, entradas no compatibles,
metadatos, reservas y destinos UDP duplicados se señalan para revisión. Las
salidas que no sean UDP no se copian. Ver
[IMPORTAR_FLUSSONIC.md](IMPORTAR_FLUSSONIC.md).

Para ver las opciones concretas también puedes ejecutar:

```bash
python3 /opt/TVStreammerSAT5/tools/import_astra_channels.py --help
python3 /opt/TVStreammerSAT5/tools/import_flussonic_channels.py --help
```

Antes de cualquier aplicación manual: guardar una copia de seguridad completa
incluida la clave de la interfaz, detener el servicio, volver a generar la copia
con la configuración actual, revisar qué canales y destinos se omitieron y
probar con uno o dos canales. No iniciar emisores simultáneos hacia el mismo
multicast; no permitir que Astra y TVStreammerSAT5 compitan por un frontend DVB.
La instalación real, reproducción y recepción UDP aún requieren pruebas.
