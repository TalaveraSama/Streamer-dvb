# Importación de canales de Cesbo Astra (en desarrollo)

`scripts/import_astra_channels.py` prepara una **vista previa**, o una copia
separada de la configuración de TVStreammerSAT5 para revisión. Nunca modifica
los archivos de entrada, no instala el resultado, no ejecuta comandos ni
reinicia servicios. Los importadores específicos están en `v203.78-rc1`; el importador único
`import_config.py` aún no se ha publicado. Ninguno se ha probado con hardware
real.

## Seguridad

Un export de Astra puede contener contraseñas, claves, usuarios y entradas
`exec://`. No lo publiques ni lo subas al repositorio. El importador rechaza
esquemas no admitidos, incluyendo `exec://`, y no copia `softcam`, `users`,
salidas, EPG ni configuración global de Astra. Rechaza URLs HTTP con usuario,
contraseña, fragmento o parámetros de consulta; no puede detectar todos los
secretos que pudieran estar ocultos en la ruta de una URL. La copia de revisión
incluye la configuración **existente de destino** y puede contener secretos: se
crea con permisos `0600`, no sobrescribe archivos existentes y debe protegerse.

Un `exec://` sospechoso en la configuración, aunque esté deshabilitado, debe
considerarse un indicio de posible compromiso, no prueba de que se ejecutó.
Investiga el equipo de forma segura antes de migrar, y rota desde un dispositivo
confiable las credenciales que hayan quedado expuestas. No ejecutes esa entrada.

## Datos y limitaciones

- DVB: convierte el SID (`pnr`), nombre, adaptador, frontend, frecuencia,
  velocidad de símbolo, polaridad, estándar, modulación y FEC admitidos.
  Los números de adaptador pueden cambiar: compruébalos antes de confirmarlos.
- Es obligatorio indicar los valores de LNB reales; no se adivinan. Cuando un
  sintonizador Astra contiene `diseqc`, es obligatorio especificar su fuente
  de DiSEqC de destino: la numeración entre aplicaciones puede ser diferente.
  Si hay valores desconocidos, la vista previa los cuenta y no se permite crear
  una copia parcial de revisión.
- HTTP/HTTPS: copia la URL solo si no contiene credenciales, parámetros de
  consulta ni fragmento. Cualquier entrada ejecutable o no admitida se rechaza
  y contabiliza; se ignoran sus salidas. Los duplicados se omiten.
- Los canales importados quedan **todos detenidos**, incluidos los que estaban
  habilitados en Astra. Las salidas UDP a `127.0.0.1`, con puertos únicos desde
  20000, son marcadores de posición, no una conversión de las salidas de Astra.
  Configura la salida real antes de arrancarlos. CAM/Newcamd debe configurarse y
  asignarse por separado si se cuenta con acceso autorizado. El EPG tampoco se
  importa. No se preserva el estado habilitado/deshabilitado por canal, solo se
  indica cuántos estaban deshabilitados en el resumen.
- Astra y TVStreammerSAT5 no deben competir por el mismo frontend DVB. Planifica
  una ventana de migración y libera el hardware antes de iniciar canales en el
  nuevo servicio. Comprueba que el VPS alcanza las fuentes HTTP privadas.

## Uso después de publicar y validar

Desde una copia segura del código, como root y con rutas locales correctas:

```bash
python3 scripts/import_astra_channels.py /ruta/export-astra.json /opt/TVStreammerSAT5/tvstreammersat5-config.json
```

Es una vista previa sin escribir archivos. Antes de generar una copia hay que
confirmar físicamente adaptadores y valores LNB. La forma es:

```text
python3 scripts/import_astra_channels.py ASTRA.json CONFIG_ACTUAL.json \
  --confirm-adapters --lof1 LOF1_MHZ --lof2 LOF2_MHZ --slof SLOF_MHZ \
  --diseqc ID_SINTONIZADOR=FUENTE_DESTINO --output NUEVA_COPIA.json
```

Sustituye los valores por los de tu instalación; el ejemplo no son comandos
listos para ejecutar. `--diseqc` se repite por cada sintonizador que lo requiera.
Sin `--output` no se escribe nada; con él se crea exclusivamente un archivo
nuevo, nunca se sobrescribe el original. El importador impide generar una copia
si faltan parámetros DVB necesarios, si el mapeo es ambiguo o si una entrada
admitida no puede convertirse. `--allow-partial` permite explícitamente omitir
los canales no convertidos. Las entradas inseguras se rechazan.

No reemplaces la configuración activa con la copia sin antes revisarla, detener
el servicio, respaldar la configuración y su archivo de clave, y regenerar la
copia desde la configuración actual para evitar perder cambios. Esta operación
requiere una ventana de mantenimiento; el cambio y la reversión deben probarse
antes en un entorno seguro.
