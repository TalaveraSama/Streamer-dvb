# Modernización de la interfaz (trabajo en curso)

## Apariencia y revisión

El panel y OSCam-mini usan un tema claro y compacto inspirado en los paneles de
monitorización de Astra, sin reutilizar su marca ni sus recursos. Se conservan
los controles y los gráficos oscuros para mantener la legibilidad de las series.
La vista previa de desarrollo con datos ficticios no controla servicios reales.

En la revisión se corrigieron el restablecimiento de ajustes no incluidos en un
POST parcial, el guardado truncado ante errores de escritura y varias inserciones
de datos sin escapar en formularios y tarjetas. La auditoría no es exhaustiva:
quedan por probar el flujo real de canales, OSCam y DVB, las actualizaciones
simultáneas de configuración y otras vistas y errores dinámicos.

## Idiomas

El botón de idioma del panel principal alterna inglés, español y ruso. La
preferencia se guarda en el navegador y se sincroniza con la configuración del
panel. Se amplió la traducción al español de los formularios de flujos, HLS,
transcodificación, DVB, MPTS, CAM, métricas y mensajes dinámicos habituales;
se conservan las opciones en inglés y ruso. Los datos de usuario, registros,
respuestas externas y algunos errores o mensajes dinámicos no catalogados pueden
seguir apareciendo en otro idioma. Se requieren pruebas visuales y funcionales
con el sistema real antes de considerar completa la traducción.

La página `/oscam-mini` dispone de selector inglés/español independiente que
comparte la preferencia del navegador. Sus controles y mensajes propios están en
inglés o español. El registro de OSCam y las respuestas de herramientas del
sistema se muestran sin alterar y pueden estar en otro idioma. Cambiar el
idioma no vuelve a cargar el formulario ni descarta los cambios pendientes.

## Mini-OSCam

La pantalla usa CSS de AdminLTE 3.2.0 (licencia MIT incluida en
`web/vendor/LICENSE.adminlte`), servido localmente sin CDN. Se conserva el
motor OSCam existente y sus funciones Newcamd, Phoenix y PC/SC; no es un motor
nuevo. Se ha corregido el guardado del estado habilitado/deshabilitado de los
lectores y de `keepalive` y `detect`. Un error de validación ya no descarta los
valores escritos en el formulario. Los archivos temporales de configuración se
crean con permisos privados antes de escribir credenciales.

Estos cambios necesitan compilación y pruebas en Ubuntu 22.04/24.04 y en un
servidor con hardware real antes de distribuirse. La instalación desde fuente,
CMake, Docker y el instalador binario copian los recursos de interfaz. El
instalador universal anterior todavía no se ha adaptado para esta versión.

## Observaciones de revisión pendientes

- Las tres configuraciones OSCam se sustituyen individualmente: un fallo a
  mitad del guardado puede dejar una actualización parcial. Respaldar la
  configuración antes de realizar cambios importantes; falta una operación
  transaccional con recuperación.
- El indicador de conexión Newcamd es una estimación basada en `ss` y no prueba
  que el usuario esté autorizado. La detección de estado del lector depende de
  textos recientes del registro y no sustituye una comprobación del hardware.
- Quedan traducciones y formularios del panel principal por revisar, además de
  pruebas funcionales de todas las operaciones con dispositivos físicos.
- No exponer el puerto 9000 directamente a Internet sin TLS; ver `SECURITY.md`.
