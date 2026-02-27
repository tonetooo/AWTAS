#include "quectel_drive.h"
#include "main.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#include "credentials.h"

static UART_HandleTypeDef *_modem_uart;
static char modem_rx_buffer[MODEM_BUFFER_SIZE];

static HAL_StatusTypeDef Modem_BringUpNetwork(void) {
    printf("[MODEM] Preparando red de datos...\r\n");

    // Aumentado a 10s para dar tiempo a la SIM tras encendido
    // Algunos modems tardan en inicializar la SIM despues del RDY
    uint32_t sim_start = HAL_GetTick();
    uint8_t sim_ready = 0;
    while (HAL_GetTick() - sim_start < 10000) {
        if (Modem_SendAT("AT+CPIN?", "READY", 1000) == HAL_OK) {
            sim_ready = 1;
            break;
        }
        HAL_Delay(1000);
    }

    if (!sim_ready) {
        printf("[MODEM] SIM no lista (Timeout 10s).\r\n");
        return HAL_ERROR;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",1", MODEM_APN, MODEM_APN_USER, MODEM_APN_PASS);
    if (Modem_SendAT(cmd, "OK", 10000) != HAL_OK) {
        printf("[MODEM] Error configurando APN.\r\n");
        return HAL_ERROR;
    }

    printf("[MODEM] Esperando registro en red...\r\n");
    uint32_t start = HAL_GetTick();
    uint8_t diag_counter = 0;
    while (HAL_GetTick() - start < 180000) { // Timeout aumentado a 3 min
        
        // Diagnostico de Estado de SIM y Operador (Cada 10s aprox)
        if (diag_counter++ % 5 == 0) {
             Modem_SendAT("AT+CPIN?", "+CPIN:", 1000);
             Modem_SendAT("AT+COPS?", "+COPS:", 1000);
             // Ver bandas configuradas por si acaso
             // Modem_SendAT("AT+QCFG=\"band\"", "OK", 1000); 
        }

        // Check Signal Quality con limpieza de buffer y parseo robusto
        memset(modem_rx_buffer, 0, MODEM_BUFFER_SIZE);
        if (Modem_SendAT("AT+CSQ", "+CSQ:", 1000) == HAL_OK) {
             // Debug: printf("[MODEM] RAW CSQ: %s\r\n", modem_rx_buffer);
             char* p = strstr(modem_rx_buffer, "+CSQ:");
             if (p) {
                 int rssi = 99, ber = 99;
                 p += 5; // Saltar "+CSQ:"
                 while(*p == ' ') p++; // Saltar espacios
                 
                 if (sscanf(p, "%d,%d", &rssi, &ber) >= 1) {
                    printf("[MODEM] Signal Quality: RSSI=%d (0-31), BER=%d\r\n", rssi, ber);
                 } else {
                    printf("[MODEM] Signal Quality Parse Error. RAW: %s\r\n", modem_rx_buffer);
                 }
             }
        }

        if (Modem_SendAT("AT+CEREG?", "+CEREG: 0,1", 2000) == HAL_OK ||
            Modem_SendAT("AT+CEREG?", "+CEREG: 0,5", 2000) == HAL_OK ||
            Modem_SendAT("AT+CREG?", "+CREG: 0,1", 2000) == HAL_OK  ||
            Modem_SendAT("AT+CREG?", "+CREG: 0,5", 2000) == HAL_OK) {
            printf("[MODEM] Registrado en red movil.\r\n");
            break;
        }
        
        // Si recibimos "0,3" (Denied), intentar recuperación
        if (strstr(modem_rx_buffer, ": 0,3")) {
             printf("[MODEM] Registro DENEGADO (0,3). Intentando reinicio de RF...\r\n");
             Modem_SendAT("AT+CFUN=0", "OK", 5000); // Modo avion
             HAL_Delay(2000);
             Modem_SendAT("AT+CFUN=1", "OK", 5000); // Modo normal
             HAL_Delay(5000); // Esperar a que inicie RF
        }
        
        HAL_Delay(2000);
    }

    if (HAL_GetTick() - start >= 180000) {
        printf("[MODEM] Tiempo de registro agotado (3 min).\r\n");
        return HAL_ERROR;
    }

    if (Modem_SendAT("AT+QIACT=1", "OK", 60000) != HAL_OK) {
        printf("[MODEM] Error activando PDP.\r\n");
        return HAL_ERROR;
    }

    printf("[MODEM] PDP activo.\r\n");
    return HAL_OK;
}

void Modem_Init(UART_HandleTypeDef *huart) {
    _modem_uart = huart;
}

HAL_StatusTypeDef Modem_PowerOn(void) {
    printf("[MODEM] Iniciando secuencia de encendido (EC25)...\r\n");
    
    // 0. Pre-check: Enviar AT por si ya esta encendido
    uint8_t dummy;
    while(HAL_UART_Receive(_modem_uart, &dummy, 1, 0) == HAL_OK); // Flush
    __HAL_UART_CLEAR_FLAG(_modem_uart, UART_FLAG_ORE | UART_FLAG_NE | UART_FLAG_FE | UART_FLAG_PE);
    
    // Intentar AT rapido
    if (Modem_SendAT("AT", "OK", 500) == HAL_OK) {
        printf("[MODEM] El modem ya estaba respondiendo. Reiniciando secuencia logica.\r\n");
        // Opcional: Podriamos asumir que esta listo, pero mejor asegurar un estado conocido
        // O simplemente retornar OK si confiamos en el estado.
        // Vamos a asumir OK para ganar tiempo, pero enviamos ATE0 por si acaso.
        Modem_SendAT("ATE0", "OK", 500);
        return HAL_OK;
    }
    
    // 1. Asegurar estado inicial APAGADO (PB0 -> HIGH)
    HAL_GPIO_WritePin(HAT_PWR_OFF_GPIO_Port, HAT_PWR_OFF_Pin, GPIO_PIN_SET);
    HAL_Delay(2000); // Aumentado a 2s para asegurar descarga
    
    // 2. ENCENDER HAT (PB0 -> LOW)
    HAL_GPIO_WritePin(HAT_PWR_OFF_GPIO_Port, HAT_PWR_OFF_Pin, GPIO_PIN_RESET);
    printf("[MODEM] HAT Energizado. Esperando estabilizacion de fuente (5s)...\r\n");
    HAL_Delay(5000); // Aumentado a 5s
    
    // 3. Pulso en PWRKEY (PB1) para encender el EC25
    printf("[MODEM] Generando pulso en PWRKEY (2s)...\r\n");
    HAL_GPIO_WritePin(MODEM_PWRKEY_GPIO_Port, MODEM_PWRKEY_Pin, GPIO_PIN_SET);
    HAL_Delay(2000); 
    HAL_GPIO_WritePin(MODEM_PWRKEY_GPIO_Port, MODEM_PWRKEY_Pin, GPIO_PIN_RESET);
    
    printf("[MODEM] Esperando inicio de firmware y RDY (Max 30s)...\r\n");
    
    uint32_t start_firmware = HAL_GetTick();
    uint32_t null_count = 0;
    char rdy_buf[3] = {0};
    uint8_t rdy_idx = 0;
    uint8_t rdy_printed = 0;

    // Aumentado timeout de espera de RDY a 30s para EC25-ADFL
    while(HAL_GetTick() - start_firmware < 30000) {
        // Verificar aborto por evento
        if (g_modem_abort_enabled && g_event_pending) {
             printf("[MODEM] Abortando PowerOn por evento.\r\n");
             return HAL_BUSY; // Usar HAL_BUSY para indicar interrupcion
        }

        uint8_t byte;
        __HAL_UART_CLEAR_FLAG(_modem_uart, UART_FLAG_ORE | UART_FLAG_NE | UART_FLAG_FE | UART_FLAG_PE);
        
        if(HAL_UART_Receive(_modem_uart, &byte, 1, 10) == HAL_OK) {
            if (byte == 0x00) { null_count++; continue; }
            null_count = 0;
            if (!rdy_printed) {
                rdy_buf[rdy_idx % 3] = (char)byte;
                rdy_idx++;
                if (rdy_buf[(rdy_idx-3)%3] == 'R' && rdy_buf[(rdy_idx-2)%3] == 'D' && rdy_buf[(rdy_idx-1)%3] == 'Y') {
                    printf("RDY\r\n");
                    rdy_printed = 1;
                    // Si recibimos RDY, podemos intentar sincronizar antes
                    break; 
                }
            }
        }
    }
    printf("\r\n");
    
    // 4. Intentar sincronizacion AT
    printf("[MODEM] Sincronizando baudrate...\r\n");
    for(int i=0; i<30; i++) { // Aumentado a 30 intentos (15s)
        if (g_modem_abort_enabled && g_event_pending) return HAL_BUSY;

        // Limpiar buffer y errores antes de cada comando AT
        uint8_t d;
        while(HAL_UART_Receive(_modem_uart, &d, 1, 0) == HAL_OK);
        __HAL_UART_CLEAR_FLAG(_modem_uart, UART_FLAG_ORE | UART_FLAG_NE | UART_FLAG_FE | UART_FLAG_PE);

        if (Modem_SendAT("AT", "OK", 1000) == HAL_OK) {
            printf("[MODEM] Comunicacion establecida: OK\r\n");
            Modem_SendAT("ATE0", "OK", 1000);
            return HAL_OK;
        } else {
            if (strlen(modem_rx_buffer) > 0) {
                printf("[MODEM] Intento %d: Recibido: ", i+1);
                for(int j=0; j<strlen(modem_rx_buffer); j++) {
                    uint8_t c = (uint8_t)modem_rx_buffer[j];
                    if(c >= 32 && c <= 126) printf("%c", c);
                    else if (c != 0) printf("[%02X]", c);
                }
                printf("\r\n");
            } else {
                printf("[MODEM] Intento %d: Sin respuesta.\r\n", i+1);
            }
        }
        HAL_Delay(500);
    }

    printf("[MODEM] Error de comunicacion inicial.\r\n");
    return HAL_ERROR;
}

void Modem_PowerOff(void) {
    printf("[MODEM] Apagando modem...\r\n");
    // Intentar apagado por software, pero no bloquear si falla
    if (Modem_SendAT("AT+QPOWD=1", "POWERED DOWN", 5000) != HAL_OK) {
        printf("[MODEM] Apagado SW fallo o sin respuesta. Forzando HW.\r\n");
    }
    HAL_Delay(2000);
    // Pin 37 en ALTO apaga el HAT
    HAL_GPIO_WritePin(HAT_PWR_OFF_GPIO_Port, HAT_PWR_OFF_Pin, GPIO_PIN_SET);
    printf("[MODEM] HAT Desenergizado (PB0 -> HIGH).\r\n");
}

HAL_StatusTypeDef Modem_SendAT(char* command, char* expected_reply, uint32_t timeout) {
    char full_cmd[128];
    snprintf(full_cmd, sizeof(full_cmd), "%s\r\n", command);
    
    memset(modem_rx_buffer, 0, MODEM_BUFFER_SIZE);
    
    // 1. Limpiar buffer de entrada UART y errores antes de enviar
    uint8_t dummy;
    while(HAL_UART_Receive(_modem_uart, &dummy, 1, 0) == HAL_OK);
    __HAL_UART_CLEAR_FLAG(_modem_uart, UART_FLAG_ORE | UART_FLAG_NE | UART_FLAG_FE | UART_FLAG_PE);

    // 2. Transmitir comando
    HAL_UART_Transmit(_modem_uart, (uint8_t*)full_cmd, strlen(full_cmd), 1000);
    
    // 3. Recepción con timeout
    uint32_t start_tick = HAL_GetTick();
    uint16_t idx = 0;
    
    while ((HAL_GetTick() - start_tick) < timeout) {
        if (g_modem_abort_enabled && g_event_pending) {
            printf("[MODEM][ABORT]\r\n");
            return HAL_ERROR;
        }
        uint8_t byte;
        // Aumentado el timeout individual a 50ms para ser mas tolerante
        if (HAL_UART_Receive(_modem_uart, &byte, 1, 50) == HAL_OK) {
            if (idx < MODEM_BUFFER_SIZE - 1) {
                modem_rx_buffer[idx++] = byte;
                modem_rx_buffer[idx] = '\0';
            }
        }
        
        // Verificar si ya tenemos la respuesta esperada
        if (strstr(modem_rx_buffer, expected_reply) != NULL) {
            return HAL_OK;
        }
        
        // Verificar errores comunes
        if (strstr(modem_rx_buffer, "ERROR") != NULL) {
            printf("[MODEM][AT ERR] Cmd='%s' Resp='%s'\r\n", command, modem_rx_buffer);
            return HAL_ERROR;
        }

        // Si hay un error de hardware durante la recepcion, limpiarlo
        if (__HAL_UART_GET_FLAG(_modem_uart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_FLAG(_modem_uart, UART_FLAG_ORE);
        }
    }
    printf("[MODEM][AT TIMEOUT] Cmd='%s' Resp='%s'\r\n", command, modem_rx_buffer);
    return HAL_TIMEOUT;
}

HAL_StatusTypeDef Modem_WaitFor(const char* expected, uint32_t timeout) {
    uint32_t start = HAL_GetTick();
    char buf[128];
    uint16_t idx = 0;
    
    // Limpiar buffer
    memset(buf, 0, sizeof(buf));

    while (HAL_GetTick() - start < timeout) {
        uint8_t c;
        // CRITICAL FIX: Limpiar ORE (Overrun) si ocurre
        if (__HAL_UART_GET_FLAG(_modem_uart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_OREFLAG(_modem_uart);
        }

        // Timeout reducido para lectura rapida
        if (HAL_UART_Receive(_modem_uart, &c, 1, 5) == HAL_OK) {
            if (idx < sizeof(buf) - 1) {
                buf[idx++] = (char)c;
                buf[idx] = 0;
            } else {
                // Buffer circular simple: mover todo 1 a la izquierda
                memmove(buf, buf + 1, sizeof(buf) - 2);
                buf[sizeof(buf) - 2] = (char)c;
                buf[sizeof(buf) - 1] = 0;
                idx = sizeof(buf) - 1;
            }
            
            // Debug opcional: putchar(c);
            
            if (strstr(buf, expected) != NULL) {
                printf("[MODEM] WaitFor='%s' Found!\r\n", expected);
                return HAL_OK;
            }
            
            if (strstr(buf, "ERROR") != NULL) {
                printf("[MODEM] WaitFor='%s' Found ERROR!\r\n", expected);
                return HAL_ERROR;
            }
        }
    }
    printf("[MODEM][AT TIMEOUT] WaitFor='%s' Resp='%s'\r\n", expected, buf);
    return HAL_TIMEOUT;
}

HAL_StatusTypeDef Modem_CheckConnection(void) {
    printf("[MODEM] Verificando registro en red...\r\n");
    if (Modem_SendAT("AT+CREG?", "+CREG: 0,1", 2000) == HAL_OK || 
        Modem_SendAT("AT+CREG?", "+CREG: 0,5", 2000) == HAL_OK) {
        printf("[MODEM] Registrado en red movil.\r\n");
        return HAL_OK;
    }
    printf("[MODEM] No registrado.\r\n");
    return HAL_ERROR;
}

HAL_StatusTypeDef Modem_UploadFile(const char* filename) {
    printf("[MODEM] Iniciando subida de %s...\r\n", filename);

    if (Modem_PowerOn() != HAL_OK) return HAL_ERROR;
    if (Modem_BringUpNetwork() != HAL_OK) {
        Modem_PowerOff();
        return HAL_ERROR;
    }

    Modem_SendAT("AT+QHTTPCFG=\"contextid\",1", "OK", 1000);
    Modem_SendAT("AT+QHTTPCFG=\"requestheader\",0", "OK", 1000);
    Modem_SendAT("AT+QHTTPCFG=\"responseheader\",0", "OK", 1000);
    Modem_SendAT("AT+QSSLCFG=\"sslversion\",1,4", "OK", 1000);
    Modem_SendAT("AT+QSSLCFG=\"seclevel\",1,0", "OK", 1000);
    Modem_SendAT("AT+QHTTPCFG=\"sslctxid\",1", "OK", 1000);

    /* Ruta 1: Google Drive directo si hay token y folder ID */
    if (GDRIVE_TOKEN[0] != 0 && GDRIVE_FOLDER_ID[0] != 0) {
        const char* url = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart";
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+QHTTPURL=%d,30", (int)strlen(url));
        if (Modem_SendAT(cmd, "CONNECT", 2000) != HAL_OK) { Modem_PowerOff(); return HAL_ERROR; }
        HAL_UART_Transmit(_modem_uart, (uint8_t*)url, strlen(url), 2000);
        Modem_SendAT("", "OK", 2000);

        char auth_hdr[256];
        snprintf(auth_hdr, sizeof(auth_hdr), "AT+QHTTPHDR=\"Authorization: Bearer %s\"", GDRIVE_TOKEN);
        if (Modem_SendAT(auth_hdr, "OK", 2000) != HAL_OK) { Modem_PowerOff(); return HAL_ERROR; }

        const char* boundary = "----AWTASBOUNDARY";
        char ct_hdr[128];
        snprintf(ct_hdr, sizeof(ct_hdr), "AT+QHTTPHDR=\"Content-Type: multipart/related; boundary=%s\"", boundary);
        if (Modem_SendAT(ct_hdr, "OK", 2000) != HAL_OK) { Modem_PowerOff(); return HAL_ERROR; }

        FIL f;
        if (f_open(&f, filename, FA_READ) != FR_OK) { Modem_PowerOff(); return HAL_ERROR; }
        DWORD fsz = f_size(&f);

        char meta[256];
        snprintf(meta, sizeof(meta), "{\"name\":\"%s\",\"parents\":[\"%s\"]}", filename, GDRIVE_FOLDER_ID);

        char pre[512];
        int pre_len = snprintf(pre, sizeof(pre),
            "--%s\r\n"
            "Content-Type: application/json; charset=UTF-8\r\n\r\n"
            "%s\r\n"
            "--%s\r\n"
            "Content-Type: text/csv\r\n\r\n",
            boundary, meta, boundary);
        const char* post_fmt = "\r\n--%s--\r\n";
        char post[64];
        int post_len = snprintf(post, sizeof(post), post_fmt, boundary);
        uint32_t total_len = (uint32_t)pre_len + fsz + (uint32_t)post_len;

        snprintf(cmd, sizeof(cmd), "AT+QHTTPPOST=%lu,60", (unsigned long)total_len);
        if (Modem_SendAT(cmd, "CONNECT", 5000) != HAL_OK) { f_close(&f); Modem_PowerOff(); return HAL_ERROR; }

        HAL_UART_Transmit(_modem_uart, (uint8_t*)pre, pre_len, 5000);

        UINT br;
        uint8_t buf[HTTP_CHUNK_SIZE];
        DWORD remaining = fsz;
        while (remaining > 0) {
            UINT to_read = remaining > HTTP_CHUNK_SIZE ? HTTP_CHUNK_SIZE : (UINT)remaining;
            if (f_read(&f, buf, to_read, &br) != FR_OK) { f_close(&f); Modem_PowerOff(); return HAL_ERROR; }
            if (br == 0) break;
            HAL_UART_Transmit(_modem_uart, buf, br, 5000);
            remaining -= br;
        }

        HAL_UART_Transmit(_modem_uart, (uint8_t*)post, post_len, 5000);
        f_close(&f);

        if (Modem_SendAT("AT+QHTTPREAD=60", "OK", 6000) != HAL_OK) { Modem_PowerOff(); return HAL_ERROR; }
        printf("[MODEM] Subida finalizada (Drive).\r\n");
        Modem_PowerOff();
        return HAL_OK;
    }

    /* Ruta 2: Backend propio si hay URL configurada */
    if (BACKEND_UPLOAD_URL[0] != 0) {
        char url[256];
        if (BACKEND_API_KEY[0] != 0) {
            snprintf(url, sizeof(url), "%s?filename=%s&key=%s", BACKEND_UPLOAD_URL, filename, BACKEND_API_KEY);
        } else {
            snprintf(url, sizeof(url), "%s?filename=%s", BACKEND_UPLOAD_URL, filename);
        }

        // Parsear URL para Host y Path (necesario para headers manuales)
        char host[128] = {0};
        char path[256] = {0};
        char* p_proto = strstr(url, "://");
        char* p_host = p_proto ? p_proto + 3 : url;
        char* p_path = strchr(p_host, '/');
        
        if (p_path) {
            size_t host_len = p_path - p_host;
            if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
            strncpy(host, p_host, host_len);
            strncpy(path, p_path, sizeof(path) - 1);
        } else {
            strncpy(host, p_host, sizeof(host) - 1);
            strcpy(path, "/");
        }

        // Configuracion SSL y HTTP explicita justo antes del uso
        Modem_SendAT("AT+QHTTPCFG=\"contextid\",1", "OK", 1000);
        Modem_SendAT("AT+QHTTPCFG=\"requestheader\",1", "OK", 1000); // Cabeceras manuales
        Modem_SendAT("AT+QHTTPCFG=\"responseheader\",1", "OK", 1000); // Ver cabeceras respuesta
        Modem_SendAT("AT+QSSLCFG=\"sslversion\",1,4", "OK", 1000);
        Modem_SendAT("AT+QSSLCFG=\"seclevel\",1,0", "OK", 1000);
        Modem_SendAT("AT+QHTTPCFG=\"sslctxid\",1", "OK", 1000);

        char cmd[64];
        // Timeout URL aumentado a 60s
        snprintf(cmd, sizeof(cmd), "AT+QHTTPURL=%d,60", (int)strlen(url));
        if (Modem_SendAT(cmd, "CONNECT", 2000) != HAL_OK) {
            Modem_PowerOff();
            return HAL_ERROR;
        }

        printf("[MODEM] URL: %s\r\n", url);
        HAL_UART_Transmit(_modem_uart, (uint8_t*)url, strlen(url), 2000);
        Modem_SendAT("", "OK", 60000); // Esperar OK de URL hasta 60s

        FIL f;
        if (f_open(&f, filename, FA_READ) != FR_OK) {
            printf("[MODEM] No se pudo abrir el archivo.\r\n");
            Modem_PowerOff();
            return HAL_ERROR;
        }

        DWORD fsz = f_size(&f);
        
        // Preparar cabeceras manuales
        char header[512];
        int header_len = snprintf(header, sizeof(header),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: text/csv\r\n"
            "Content-Length: %lu\r\n"
            "\r\n",
            path, host, (unsigned long)fsz);
            
        uint32_t total_len = header_len + fsz;

        // Timeout POST aumentado a 120s, wait time 120s
        snprintf(cmd, sizeof(cmd), "AT+QHTTPPOST=%lu,120,120", (unsigned long)total_len);
        if (Modem_SendAT(cmd, "CONNECT", 15000) != HAL_OK) {
            f_close(&f);
            Modem_PowerOff();
            return HAL_ERROR;
        }

        // Enviar Cabeceras
        HAL_UART_Transmit(_modem_uart, (uint8_t*)header, header_len, 2000);

        // Enviar Archivo
        UINT br;
        uint8_t buf[HTTP_CHUNK_SIZE];
        DWORD remaining = fsz;
        while (remaining > 0) {
            UINT to_read = remaining > HTTP_CHUNK_SIZE ? HTTP_CHUNK_SIZE : (UINT)remaining;
            if (f_read(&f, buf, to_read, &br) != FR_OK) {
                f_close(&f);
                Modem_PowerOff();
                return HAL_ERROR;
            }
            if (br == 0) break;
            HAL_UART_Transmit(_modem_uart, buf, br, 5000);
            remaining -= br;
        }

        f_close(&f);
        HAL_Delay(1000);
        
        // Esperar respuesta (120s)
        // OJO: Con AT+QHTTPCFG="responseheader",1, el modem primero imprime los headers
        // y luego el +QHTTPPOST: 0,200,...
        // Si usamos Modem_WaitFor("+QHTTPPOST:"), podemos perdernos si llega mezclado.
        
        HAL_StatusTypeDef res = Modem_WaitFor("+QHTTPPOST:", 120000);
        
        // Si hay respuesta HTTP pero Modem_WaitFor no la capturo bien (por buffer circular o lo que sea),
        // intentamos leer de todas formas.
        
        printf("[MODEM] Leyendo respuesta del servidor...\r\n");
        // AT+QHTTPREAD= wait_time
        Modem_SendAT("AT+QHTTPREAD=60", "OK", 60000); 

        // Analisis de respuesta mas robusto:
        // Buscamos "+QHTTPPOST:" en todo el buffer acumulado o en la ultima lectura
        char* p = strstr(modem_rx_buffer, "+QHTTPPOST:");
        
        // Si no esta en el buffer actual, quizas ya paso.
        // Pero el log muestra: [MODEM] RAW QHTTPPOST: CONNECT HTTP/1.1 200 OK ...
        // El modem esta devolviendo los HEADERS directamente al UART porque activamos responseheader=1
        // Y el URC +QHTTPPOST: quizas viene DESPUES o ANTES.
        
        // Si vemos "HTTP/1.1 200 OK" en el log, es EXITO, aunque +QHTTPPOST no se parsee bien.
        if (strstr(modem_rx_buffer, "200 OK") != NULL || strstr(modem_rx_buffer, "HTTP/1.0 200 OK") != NULL) {
             printf("[MODEM] Detectado 200 OK en headers. Subida exitosa.\r\n");
             printf("[MODEM] Subida finalizada (backend).\r\n");
             Modem_PowerOff();
             return HAL_OK;
        }

        if (res != HAL_OK && p == NULL) {
            printf("[MODEM] Fallo en POST (Timeout o Error).\r\n");
            Modem_PowerOff();
            return res;
        }

        printf("[MODEM] RAW QHTTPPOST: %s\r\n", modem_rx_buffer);
        int http_result = -1;
        int http_status = 0;
        
        if (p) {
            p += strlen("+QHTTPPOST:");
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            char* c1 = strchr(p, ',');
            char* c2 = c1 ? strchr(c1 + 1, ',') : NULL;
            if (c1) {
                http_result = atoi(p);
                // Si hay 3 argumentos: err, http_code, content_len
                // Si hay 2 argumentos: err, http_code
                if (c2) {
                     // Formato err,code,len
                    http_status = atoi(c1 + 1);
                } else {
                     // Formato err,code
                    http_status = atoi(c1 + 1);
                }
            }
        }
        
        // Fallback: Si no pudimos parsear +QHTTPPOST pero vimos 200 OK antes, ya retornamos OK.
        // Si llegamos aqui, es que no vimos 200 OK claro.
        
        printf("[MODEM] HTTP Result: %d, Status: %d\r\n", http_result, http_status);
        
        // Si el resultado es 0 (Exito modem) y status 200, todo bien.
        if (http_result == 0 && (http_status == 200 || http_status == 0)) {
             // A veces status es 0 si no se pudo leer el codigo pero el comando dio OK
             printf("[MODEM] Subida finalizada (backend).\r\n");
             Modem_PowerOff();
             return HAL_OK;
        }

        if (http_result != 0) {
            Modem_PowerOff();
            return HAL_ERROR;
        }
        if (http_status != 0 && (http_status < 200 || http_status >= 300)) {
            printf("[MODEM] HTTP Status fuera de rango: %d\r\n", http_status);
            Modem_PowerOff();
            return HAL_ERROR;
        }
        printf("[MODEM] Subida finalizada (backend).\r\n");
        Modem_PowerOff();
        return HAL_OK;
    }

    printf("[MODEM] Credenciales no configuradas (Drive/Backend).\r\n");
    Modem_PowerOff();
    return HAL_ERROR;
}

HAL_StatusTypeDef Modem_DownloadConfig(char* out_buffer, uint16_t out_size) {
    if (BACKEND_CONFIG_URL[0] == 0) {
        printf("[MODEM] BACKEND_CONFIG_URL no configurado.\r\n");
        return HAL_ERROR;
    }
    if (out_buffer == NULL || out_size == 0) {
        return HAL_ERROR;
    }
    if (Modem_BringUpNetwork() != HAL_OK) {
        Modem_PowerOff();
        return HAL_ERROR;
    }
    Modem_SendAT("AT+QHTTPCFG=\"contextid\",1", "OK", 1000);
    Modem_SendAT("AT+QHTTPCFG=\"requestheader\",0", "OK", 1000);
    Modem_SendAT("AT+QHTTPCFG=\"responseheader\",0", "OK", 1000);
    Modem_SendAT("AT+QSSLCFG=\"sslversion\",1,4", "OK", 1000);
    Modem_SendAT("AT+QSSLCFG=\"seclevel\",1,0", "OK", 1000);
    Modem_SendAT("AT+QHTTPCFG=\"sslctxid\",1", "OK", 1000);
    char url[256];
    if (BACKEND_API_KEY[0] != 0) {
        snprintf(url, sizeof(url), "%s?name=AWTAS_CONFIG.TXT&key=%s&compact=1", BACKEND_CONFIG_URL, BACKEND_API_KEY);
    } else {
        snprintf(url, sizeof(url), "%s?name=AWTAS_CONFIG.TXT&compact=1", BACKEND_CONFIG_URL);
    }
    printf("[MODEM] CFG URL: %s\r\n", url);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QHTTPURL=%d,30", (int)strlen(url));
    if (Modem_SendAT(cmd, "CONNECT", 2000) != HAL_OK) {
        Modem_PowerOff();
        return HAL_ERROR;
    }
    HAL_UART_Transmit(_modem_uart, (uint8_t*)url, strlen(url), 2000);
    Modem_SendAT("", "OK", 2000);
    if (Modem_SendAT("AT+QHTTPGET=60", "+QHTTPGET:", 60000) != HAL_OK) {
        Modem_PowerOff();
        return HAL_ERROR;
    }
    int http_code = 0;
    char* p = strstr(modem_rx_buffer, "+QHTTPGET:");
    if (p != NULL) {
        char* c1 = strchr(p, ',');
        char* c2 = c1 ? strchr(c1 + 1, ',') : NULL;
        if (c1 != NULL && c2 != NULL) {
            http_code = atoi(c1 + 1);
        }
    }
    printf("[MODEM] CFG HTTP Status: %d\r\n", http_code);
    if (http_code != 0 && (http_code < 200 || http_code >= 300)) {
        Modem_PowerOff();
        return HAL_ERROR;
    }
    const char* read_cmd = "AT+QHTTPREAD=60\r\n";
    HAL_UART_Transmit(_modem_uart, (uint8_t*)read_cmd, strlen(read_cmd), 1000);
    uint32_t start = HAL_GetTick();
    size_t w = 0;
    while ((HAL_GetTick() - start) < 60000) {
        uint8_t b;
        // CRITICAL FIX: Limpiar ORE (Overrun) si ocurre
        if (__HAL_UART_GET_FLAG(_modem_uart, UART_FLAG_ORE)) {
            __HAL_UART_CLEAR_FLAG(_modem_uart, UART_FLAG_ORE);
        }
        
        // Timeout muy corto para polling rapido
        if (HAL_UART_Receive(_modem_uart, &b, 1, 10) == HAL_OK) {
            if (w < out_size - 1) {
                out_buffer[w++] = (char)b;
            } else {
                // Buffer lleno, seguir leyendo para vaciar FIFO pero no guardar
            }
        }
    }

    if (w >= out_size) w = out_size - 1;
    out_buffer[w] = 0;
    
    // Fix para posible corrupcion de buffer (FILEANUALMATFIL_AUO=MT)
    // Asegurar terminacion de linea y limpieza
    // IMPORTANTE: NO reemplazar \r ni \n, son necesarios para strtok
    for(size_t k=0; k<w; k++) {
        // Permitir tab (9), CR (13), LF (10) y caracteres imprimibles (>=32)
        if(out_buffer[k] < 32 && out_buffer[k] != '\r' && out_buffer[k] != '\n' && out_buffer[k] != '\t') {
            out_buffer[k] = ' '; // Reemplazar caracteres no imprimibles con espacios
        }
    }
    
    printf("[MODEM] CFG bytes: %lu\r\n", (unsigned long)w);
    
    // DEBUG: Imprimir buffer crudo para ver que esta llegando
    printf("[MODEM] RAW Config Buffer:\r\n%s\r\n", out_buffer);
    
    // Buscar codigo HTTP en "+QHTTPGET: 0,200,..." en el buffer anterior (si existe)
    // Pero out_buffer contiene el contenido, no el header.
    // Usamos el http_code obtenido antes de la lectura de contenido.
    
    // Si http_code es 0, intentamos ver si obtuvimos contenido valido de todas formas.
    // A veces el modem no devuelve el URC correctamente pero si el contenido.
    
    printf("[MODEM] CFG HTTP Status (pre-read): %d\r\n", http_code);

    // CRITICAL FIX: Solo fallar si no hay contenido O si el codigo es error explicito (no 0)
    // Si http_code es 0 pero tenemos bytes, asumimos que puede ser valido y parseamos.
    if (http_code != 0 && (http_code < 200 || http_code >= 300)) {
        printf("[MODEM] Error HTTP %d en descarga config.\r\n", http_code);
        Modem_PowerOff();
        return HAL_ERROR;
    }
    
    // Parsear pares CLAVE=VALOR
    int keys = 0;
    
    // IMPORTANTE: strtok modifica el string original. 
    // Si el formato de linea es incorrecto, strtok puede fallar.
    // Vamos a usar una copia o un metodo mas seguro si strtok falla.
    
    char* line = strtok(out_buffer, "\r\n");
    while(line) {
        // Ignorar CONNECT si aparece al principio
        if (strstr(line, "CONNECT") == line) {
            line = strtok(NULL, "\r\n");
            continue;
        }
        
        char* eq = strchr(line, '=');
        if(eq) {
            *eq = 0;
            char* key = line;
            char* val = eq + 1;
            
            // Trim spaces key
            while(*key == ' ' || *key == '\t') key++;
            // Trim spaces val
            while(*val == ' ' || *val == '\t') val++;
            
            // Guardar en estructura global
            printf("[MODEM] Applying %s=%s\r\n", key, val);
            Apply_Remote_Config(key, val);
            keys++;
        }
        line = strtok(NULL, "\r\n");
    }
    
    printf("[MODEM] CFG keys found: %d\r\n", keys);
    Modem_PowerOff();
    return (keys > 0) ? HAL_OK : HAL_ERROR;
}
