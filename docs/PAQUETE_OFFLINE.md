# Paquete binario sin descarga durante la instalación

Los ZIP se compilan por separado para Ubuntu 20.04, 22.04 y 24.04 x86_64.
20.04 es experimental. Cada ZIP incluye el panel, OSCam-mini, el plugin CA,
recursos web locales, importadores, configuraciones iniciales y unidades systemd.

**No es una distribución autónoma:** no incluye paquetes .deb, dependencias del
sistema, controladores ni todos los plugins GStreamer. Instale previamente las
bibliotecas necesarias y `gst-inspect-1.0` para la versión correspondiente; un
servidor que ya compila y ejecuta el proyecto suele tenerlas. También pueden ser
necesarios plugins adicionales para los protocolos usados y pcscd para PC/SC.
Sin estas dependencias el paquete no sirve para una instalación completamente
sin conexión desde cero. No instala dependencias ni ejecuta apt.

Descargue el ZIP y su SHA256SUMS en un equipo con acceso a Internet y transfiéralos
al VPS. Compruebe su integridad con `sha256sum -c SHA256SUMS` (si se descargó
un solo ZIP, los demás nombres del archivo se informarán como ausentes; se puede
seleccionar su línea con `grep 'NOMBRE-DEL-ZIP' SHA256SUMS | sha256sum -c`).
El SHA256SUMS acompaña al release pero no constituye una firma independiente.
Descomprima con `unzip` o `python3 -m zipfile -e paquete.zip carpeta`.
Dentro de la carpeta, ejecute `bash install_offline.sh --check` sin root; para
instalar ejecute `bash install_offline.sh --install` como root. El chequeo no
comprueba todos los plugins, drivers ni la compatibilidad real con hardware.

El instalador rechaza servicios TVStreammerSAT5 u OSCam-mini activos: detenerlos
puede interrumpir transmisiones y decodificación. No inicia ni habilita servicios.
Crea respaldo privado en /root antes de reemplazar binarios y preserva los archivos
de configuración, unidades y overrides existentes. No borre ese respaldo. En caso
de error, el instalador puede haber reemplazado solo algunos archivos; revise el
error y restaure desde el respaldo antes de arrancar. Verifique las unidades
existentes y sus rutas antes de arrancar manualmente. En una instalación nueva,
el primer arranque genera credenciales iniciales en el registro del servicio.
No importa canales ni modifica Astra o Flussonic.

Si quiere actualizar una instalación en producción, programe una ventana de
mantenimiento, verifique sus respaldos y pruebe uno o dos canales individualmente.
Los flujos configurados con auto_start pueden arrancar al iniciar el servicio;
evite conflictos multicast con otros emisores.
