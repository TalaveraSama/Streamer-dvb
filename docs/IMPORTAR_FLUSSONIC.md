# Importación de canales Flussonic (en desarrollo)

`scripts/import_flussonic_channels.py` analiza una configuración de texto de
Flussonic y prepara una vista previa o un archivo de revisión separado. No
modifica la configuración activa, no ejecuta los valores del archivo ni arranca
los canales. Se ha probado con ejemplos sintéticos, no en un sistema real. La utilidad
específica está en `v203.78-rc1`; el importador único `import_config.py` aún
no se ha publicado.

## Alcance

- Conserva el identificador y el título (`title`) de cada stream y, cuando se
  indica, su `provider`. Omite los IDs ya existentes en destino.
- Convierte destinos `push udp://INTERFAZ@IP:PUERTO` a UDP VBR con la misma IP
  y puerto. Se requiere confirmar explícitamente un mapeo de cada interfaz de
  origen a un nombre o IPv4 de la máquina de destino. El motor UDP puede resolver
  nombres de interfaz, pero hay que comprobar que existan en esa máquina.
- **Todos los streams se crean sin arranque automático**, aunque en Flussonic
  estuvieran habilitados. Los que no tengan push UDP reciben una salida local
  provisional que debe configurarse manualmente. Se muestra un resumen con el
  número de streams originalmente deshabilitados.
- Destinos multicast repetidos entre streams o con destinos existentes se
  señalan. La copia no se genera salvo confirmación explícita; incluso después
  de confirmar, nunca inicies a la vez dos emisores para el mismo destino sin
  revisar el diseño. Revisa también los que ya estén transmitiendo en otro
  servidor.
- `tshttp://` se convierte a `http://`; `hls://` a HTTP y `hlss://` a HTTPS.
  Por defecto, `m4f://servidor:puerto/ruta` se convierte a
  `http://servidor:puerto/ruta/index.m3u8`, en modo HLS, según la convención
  confirmada para esta instalación. Se conserva la ruta de origen, que puede
  diferir del identificador o título. El host y el puerto no se cambian.
  Se puede usar `--m4f-template` para una plantilla alternativa verificada
  con `{host}`, `{port}` y `{path}`. La conversión no garantiza que la fuente
  exponga HLS: verifica que la URL sea accesible desde el servidor de destino.
- Las URLs con consultas se rechazan por defecto porque pueden contener tokens.
  `--allow-query` las permite expresamente; esas URLs y tokens se guardarán en
  texto en el JSON de destino y pueden aparecer en el panel o en registros. No
  uses esta opción sin entender y aceptar ese riesgo. Se rechazan usuarios y
  contraseñas dentro de las URL en cualquier caso.
- Solo se aceptan una entrada o dos; para la segunda debe confirmarse el orden
  principal/respaldo y deben ser compatibles los modos de entrada. El failover
  podría comportarse de forma diferente a Flussonic y debe probarse.
- `publish://` y otros protocolos no admitidos requieren configuración manual;
  no se ejecutan. Los `push` que no sean UDP y opciones como `prepush` se
  contabilizan pero no se convierten. Las directivas `meta` requieren revisión
  y solo se omiten mediante confirmación explícita. No se importan ajustes
  globales, credenciales, RTMP push, EPG ni otras funciones de Flussonic.

La configuración de Flussonic puede contener contraseñas y tokens. No subas el
archivo al repositorio ni lo publiques. El archivo de revisión contiene la
configuración actual de TVStreammerSAT5, incluidos posibles secretos; se crea
con modo `0600`, no sobrescribe archivos existentes y debe protegerse. Rota las
credenciales que se hayan compartido accidentalmente.

## Recibir los streams desde un Flussonic local

Con `--local-hls-base http://192.168.160.7:2086` el importador construye la
entrada de **cada canal** como `http://192.168.160.7:2086/ID/index.m3u8`, usando
el ID del bloque `stream`, no la URL upstream de `input`. No copia ninguna
entrada ni respaldo original; Flussonic gestiona sus fuentes y respaldos. Este
modo también incluye streams con entradas no compatibles o sin `input`: solo
funcionarán si Flussonic publica HLS en esa ruta, con acceso desde el servidor.
Los streams deshabilitados en Flussonic pueden no estar disponibles.

Ejemplo de vista previa, únicamente si la interfaz UDP de origen es `ens37` y
la de salida correcta en destino es `ens36`:

```bash
python3 /opt/TVStreammerSAT5/tools/import_config.py /home/flussonic.txt \
  /opt/TVStreammerSAT5/tvstreammersat5-config.json \
  --local-hls-base http://192.168.160.7:2086 --interface ens37=ens36 --details
```

Este modo no copia autenticación, opciones de origen ni respaldos al destino;
la URL base no admite credenciales, ruta ni consultas. Se mantiene el IP y
puerto de cada salida UDP, con la interfaz mapeada. No actives dos emisores al
mismo destino UDP (incluido Flussonic). Primero revisa las advertencias y
verifica una ruta HLS y la red multicast; nada se activa automáticamente.

## Vista previa y archivo de revisión

Después de publicar y validar la versión del proyecto, ejecutar la vista previa
sin `--output` no escribe ningún archivo:

```text
python3 scripts/import_flussonic_channels.py FLUSSONIC.conf CONFIG_ACTUAL.json
```

Para una copia de revisión se requiere resolver las advertencias. La forma de
los argumentos es la siguiente; **no son valores listos para ejecutar**:

```text
python3 scripts/import_flussonic_channels.py FLUSSONIC.conf CONFIG_ACTUAL.json \
  --interface INTERFAZ_ORIGEN=INTERFAZ_DESTINO \
  --output COPIA_NUEVA.json
```

Repite `--interface` por cada interfaz. `--m4f-template` permite configurar
una ruta HLS alternativa; sin él se usa la conversión indicada arriba. Los argumentos opcionales
`--confirm-backup-order`, `--omit-meta`, `--allow-query` y
`--allow-duplicate-udp` implican las decisiones y riesgos descritos arriba.
Si existen entradas incompatibles no se genera el archivo salvo que se acepten
las omisiones con `--allow-partial`; los conflictos UDP requieren confirmación
adicional. Las entradas y
salidas no convertidas deben revisarse y configurarse manualmente.

No reemplaces la configuración activa sin respaldo, ventana de mantenimiento y
revisión del archivo y de su correspondiente clave; vuelve a generar la copia
con la configuración actual cuando el servicio esté detenido para evitar
perder cambios concurrentes. Valida primero uno o dos canales y la recepción
UDP antes de cambiar el resto.
