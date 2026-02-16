# AWTAS_DEFINITIVE

Sistema autónomo de adquisición triaxial (AWTAS) sobre STM32F446RE. Integra sensado con ADXL355, registro en SD (CSV) y subida de archivos a Google Drive mediante un backend HTTP accesible por túnel Cloudflare usando un módem Quectel EC25.

## Características

- Sensor ADXL355 (SPI): lectura continua, configuración de rango (±2g/±4g/±8g) y ODR (31.25–4000 Hz).
- Registro en SD (FatFs, SPI1): archivos CSV con encabezado y nombres auto-generados.
- Modo “Wake-on-Motion”: armado por umbral y captura de evento, con mensaje de estado y “settling” posterior.
- Menú UART interactivo en la consola (USART2 a 115200):
  - m: Monitor de datos (G)
  - l: Log de datos a CSV
  - r: Ajustar rango
  - o: Ajustar ODR
  - t: Ajustar umbral de disparo
  - i: Modo de interrupción (Wake-on-Motion)
  - q: Stop/Back
- Subida a Google Drive (opcional) al finalizar la captura:
  - Backend Flask local expuesto por Cloudflare Tunnel (HTTPS)
  - Autenticación simple por API key
- Módem Quectel EC25:
  - Secuencia de encendido (PWRKEY), sincronización AT y registro en red
  - Configuración de APN y activación de PDP
  - HTTP POST hacia el backend con confirmación por código (+QHTTPPOST)

## Flujo de trabajo

1. Conecta el ADXL355 y la SD según el pinout del Nucleo-F446RE.
2. Abre el menú por UART (USART2). Selecciona “l” para logging o “i” para modo interrupt.
3. Al detener, el sistema pregunta si deseas subir el CSV a Drive. Responde “s” para iniciar el proceso con el módem.
4. El backend recibe el CSV y lo guarda en la carpeta configurada de Google Drive.

## Estructura principal

- Core/Src/main.c: bucle principal, menú, logging y pregunta de subida.
- Core/Src/adxl355.c: driver y utilidades del sensor.
- Core/Src/sd_spi.c, FATFS/*: manejo de SD y CSV.
- Core/Src/quectel_drive.c: control del EC25, red y HTTP POST.
- Core/Inc/credentials.h: configuración del backend (URL y API key) y APN.

## Configuración

Editar `Core/Inc/credentials.h`:

- BACKEND_UPLOAD_URL: ruta HTTPS del backend (por ejemplo, `https://...trycloudflare.com/upload`).
- BACKEND_API_KEY: clave esperada por el backend (cabecera o query string).
- MODEM_APN/MODEM_APN_USER/MODEM_APN_PASS: datos del operador celular.

El backend Flask lee variables de entorno:

- DEVICE_API_KEY: API key esperada.
- DRIVE_FOLDER_ID: carpeta destino en Drive.

## Backend (Drive)

Repositorio separado recomendado. Estructura típica:

- main.py: endpoint `/upload` (POST) que valida API key, recibe CSV y crea archivo en Drive.
- requirements.txt: dependencias (flask, google-api-python-client, etc.).
- start.sh: lanza Flask y Cloudflare Tunnel; registra URL generada.

Uso rápido:

```bash
bash start.sh
# Revisa backend.log y tunnel.log; copia la URL del túnel a BACKEND_UPLOAD_URL
```

## Subida con EC25

- Configura contextid y SSL según el firmware.
- Envía la URL con `filename` y `key` por query string.
- Realiza `QHTTPPOST` del cuerpo CSV.
- Valida éxito con `+QHTTPPOST: 0,200,<len>` y muestra “Subida finalizada (backend)”. No se requiere `QHTTPREAD`.

## CSV

- Encabezado: `timestamp_rel_s;timestamp_abs;unix_time;x_g;y_g;z_g;voltaje;corriente;potencia`
- Delimitador: `;` (compatibilidad regional con decimales punto).
- Nombres: `DATA_XXX.CSV` para logging manual; `TRIG_XXX.CSV` en eventos (si corresponde).

## Construcción

- STM32CubeIDE (F446RE), estándar HAL.
- Asegurar USART2 para consola y SPI1 para SD.
- Implementar `__io_putchar` para `printf` en consola.

## Notas

- No publicar secretos: el backend debe cargar credenciales de Drive vía variables de entorno; no incluir `credentials.json` en el repo público.
- Cloudflare Tunnel genera URL temporal; actualizar `BACKEND_UPLOAD_URL` cuando cambie.
- El prompt UART puede mostrar eco local si tecleas antes de ver el mensaje; funcionalmente no afecta.

