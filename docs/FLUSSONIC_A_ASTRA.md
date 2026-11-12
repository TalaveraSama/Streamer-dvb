# Preparar copia Astra con canales de Flussonic

`scripts/flussonic_to_astra.py` crea un plan o una **copia JSON para revisión**;
no realiza llamadas a Astra, no edita los archivos de entrada ni aplica la copia,
y no arranca, detiene o reinicia servicios. Se ha probado con datos sintéticos,
no con la instalación real ni con todas las versiones de Astra. El ejemplo de
usuario usa Astra 2025-06-12, `gid` numérico y una lista `make_stream`.

## Datos y seguridad

- Para cada `stream ID`, la entrada es `BASE/ID/index.m3u8`. La fuente original,
  su autenticación y sus respaldos no se copian; los gestiona Flussonic. Que un
  manifiesto responda HTTP 200 no prueba el funcionamiento ni los demás streams.
- Las salidas `push udp://INTERFAZ@IP:PUERTO` se conservan con el mapeo
  explícito de interfaz, y se admiten múltiples salidas. Cuando no hay interfaz
  en el origen, se usa la de destino indicada. Los push no UDP, metadatos y
  demás directivas se omiten y se cuentan; se deben revisar. Se mantiene el
  nombre `title` o, si no existe, el ID de Flussonic.
- Se generan IDs Astra únicos en base 36 desde `gid`; no se usa el ID de
  Flussonic como identificador Astra (sí en la URL). Los nuevos canales son
  `enable:false`; los sin UDP quedan con `output:[]`. No hay salida provisional.
- Una entrada HLS ya existente con la misma URL se omite sin modificarla. Se
  ignoran consultas y fragmentos en esa comparación para evitar duplicados;
  comprueba manualmente los omitidos y su configuración. Un nombre ya existente
  con distinta entrada bloquea la creación de la copia por ambigüedad.
- Se comprueban destinos UDP y RTP de los streams existentes (incluso los
  deshabilitados), además de los canales nuevos. Los conflictos se informan por
  IP y puerto, independientemente de la interfaz. No se detectan los emisores
  externos de Flussonic, TVStreammerSAT5 u otras máquinas: revísalos aparte.
  Salidas existentes UDP/RTP no interpretables bloquean el proceso.
- La copia **conserva íntegros los canales, estados y ajustes existentes**. Por
  ejemplo, un canal que estaba habilitado sigue habilitado; únicamente los nuevos
  están detenidos. Copia también usuarios, contraseñas y cualquier configuración
  preexistente potencialmente insegura (incluyendo `exec://`), sin ejecutarla.
  No representa una auditoría ni limpia la configuración. No subas el export ni
  la copia al repositorio, al chat ni a servicios públicos.
- La copia se crea exclusivamente como archivo nuevo `0600`; nunca sobreescribe
  un destino existente. Algunos fallos de entrada impiden crear una copia parcial.
  El programa no instala, activa ni verifica el archivo con Astra real.

## Vista previa

Exporta la configuración completa y actual desde Astra y guárdala en un archivo
privado. No pegues el contenido en el chat. Este ejemplo usa una ruta de muestra;
no asume que coincida con la ubicación del archivo activo en Astra.

```bash
python3 scripts/flussonic_to_astra.py /home/flussonic.txt /root/astra-export.json \
  --local-hls-base http://192.168.160.7:2086 --interface ens37=ens36 --details
```

No escribe archivos. Verifica que `ens36` exista y sirva la red correcta. Revisa
los IDs omitidos, los nombres repetidos, las directivas no convertidas y **cada**
conflicto, incluyendo transmisiones desde otros programas. Hay que revisar por
identidad; el mero número de conflictos no demuestra que sean los esperados.

Solo tras revisar todo, se puede crear un archivo privado aparte con `--output
/ruta/archivo-nuevo.json`. Si se aceptan conflictos concretos, `--allow-duplicate-udp`
permite **todos** los conflictos detectados, no solo los seis previamente
identificados; no activarlos al mismo tiempo. Repite la vista previa desde un
export actualizado si Astra cambia. La copia puede contener credenciales y se
debe almacenar protegida. No se debe aplicar sin confirmar el mecanismo de
importación de esa versión, respaldo, ventana de mantenimiento y pruebas en
entorno aislado. La copia incluye las opciones existentes, no solo los canales.
