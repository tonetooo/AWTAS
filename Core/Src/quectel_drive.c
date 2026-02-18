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

    if (Modem_SendAT("AT+CPIN?", "READY", 5000) != HAL_OK) {
        printf("[MODEM] SIM no lista.\r\n");
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
    while (HAL_GetTick() - start < 60000) {
        if (Modem_SendAT("AT+CEREG?", "+CEREG: 0,1", 2000) == HAL_OK ||
            Modem_SendAT("AT+CEREG?", "+CEREG: 0,5", 2000) == HAL_OK ||
            Modem_SendAT("AT+CREG?", "+CREG: 0,1", 2000) == HAL_OK  ||
            Modem_SendAT("AT+CREG?", "+CREG: 0,5", 2000) == HAL_OK) {
            printf("[MODEM] Registrado en red movil.\r\n");
            break;
        }
        HAL_Delay(1000);
    }

    if (HAL_GetTick() - start >= 60000) {
        printf("[MODEM] Tiempo de registro agotado.\r\n");
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
    printf("[MODEM] Iniciando secuencia de encendido...\r\n");
    
    // 1. Asegurar estado inicial APAGADO (PB0 -> HIGH)
    HAL_GPIO_WritePin(HAT_PWR_OFF_GPIO_Port, HAT_PWR_OFF_Pin, GPIO_PIN_SET);
    HAL_Delay(1000); 
    
    // 2. ENCENDER HAT (PB0 -> LOW)
    HAL_GPIO_WritePin(HAT_PWR_OFF_GPIO_Port, HAT_PWR_OFF_Pin, GPIO_PIN_RESET);
    printf("[MODEM] HAT Energizado. Esperando estabilizacion de fuente (3s)...\r\n");
    HAL_Delay(3000); 
    
    // 3. Pulso en PWRKEY (PB1) para encender el EC25
    printf("[MODEM] Generando pulso en PWRKEY (2s)...\r\n");
    HAL_GPIO_WritePin(MODEM_PWRKEY_GPIO_Port, MODEM_PWRKEY_Pin, GPIO_PIN_SET);
    HAL_Delay(2000); 
    HAL_GPIO_WritePin(MODEM_PWRKEY_GPIO_Port, MODEM_PWRKEY_Pin, GPIO_PIN_RESET);
    
    printf("[MODEM] Esperando inicio de firmware y RDY (15s)...\r\n");
    
    uint32_t start_firmware = HAL_GetTick();
    uint8_t last_byte = 0xFF;
    uint32_t null_count = 0;

    char rdy_buf[3] = {0};
    uint8_t rdy_idx = 0;
    uint8_t rdy_printed = 0;
    while(HAL_GetTick() - start_firmware < 15000) {
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
                }
            }
        }
    }
    printf("\r\n");
    
    // 4. Intentar sincronizacion AT
    printf("[MODEM] Sincronizando baudrate...\r\n");
    for(int i=0; i<20; i++) { 
        // Limpiar buffer y errores antes de cada comando AT
        uint8_t dummy;
        while(HAL_UART_Receive(_modem_uart, &dummy, 1, 0) == HAL_OK);
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
    Modem_SendAT("AT+QPOWD=1", "POWERED DOWN", 2000);
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

static HAL_StatusTypeDef Modem_WaitFor(const char* token, uint32_t timeout) {
    memset(modem_rx_buffer, 0, MODEM_BUFFER_SIZE);
    uint32_t start_tick = HAL_GetTick();
    uint16_t idx = 0;
    while ((HAL_GetTick() - start_tick) < timeout) {
        uint8_t byte;
        if (HAL_UART_Receive(_modem_uart, &byte, 1, 50) == HAL_OK) {
            if (idx < MODEM_BUFFER_SIZE - 1) {
                modem_rx_buffer[idx++] = byte;
                modem_rx_buffer[idx] = '\0';
            }
            if (strstr(modem_rx_buffer, token) != NULL) {
                return HAL_OK;
            }
            if (strstr(modem_rx_buffer, "ERROR") != NULL) {
                printf("[MODEM][AT ERR] WaitFor='%s' Resp='%s'\r\n", token, modem_rx_buffer);
                return HAL_ERROR;
            }
        }
    }
    printf("[MODEM][AT TIMEOUT] WaitFor='%s' Resp='%s'\r\n", token, modem_rx_buffer);
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

        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+QHTTPURL=%d,30", (int)strlen(url));
        if (Modem_SendAT(cmd, "CONNECT", 2000) != HAL_OK) {
            Modem_PowerOff();
            return HAL_ERROR;
        }

        printf("[MODEM] URL: %s\r\n", url);
        HAL_UART_Transmit(_modem_uart, (uint8_t*)url, strlen(url), 2000);
        Modem_SendAT("", "OK", 2000);

        FIL f;
        if (f_open(&f, filename, FA_READ) != FR_OK) {
            printf("[MODEM] No se pudo abrir el archivo.\r\n");
            Modem_PowerOff();
            return HAL_ERROR;
        }

        DWORD fsz = f_size(&f);
        snprintf(cmd, sizeof(cmd), "AT+QHTTPPOST=%lu,60", (unsigned long)fsz);
        if (Modem_SendAT(cmd, "CONNECT", 5000) != HAL_OK) {
            f_close(&f);
            Modem_PowerOff();
            return HAL_ERROR;
        }

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
        if (Modem_WaitFor("+QHTTPPOST:", 60000) != HAL_OK) {
            Modem_PowerOff();
            return HAL_ERROR;
        }
        int http_code = 0;
        char* p = strstr(modem_rx_buffer, "+QHTTPPOST:");
        if (p) {
            char* c1 = strchr(p, ',');
            char* c2 = c1 ? strchr(c1 + 1, ',') : NULL;
            if (c1 && c2) { http_code = atoi(c1 + 1); }
        }
        if (http_code < 200 || http_code >= 300) {
            printf("[MODEM] HTTP Status: %d\r\n", http_code);
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
    if (BACKEND_UPLOAD_URL[0] == 0) {
        printf("[MODEM] BACKEND_UPLOAD_URL no configurado.\r\n");
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
    char base[256];
    memset(base, 0, sizeof(base));
    strncpy(base, BACKEND_UPLOAD_URL, sizeof(base) - 1);
    char* slash = strrchr(base, '/');
    if (slash != NULL) {
        slash[1] = 0;
        strncat(base, "config", sizeof(base) - strlen(base) - 1);
    } else {
        strncat(base, "/config", sizeof(base) - strlen(base) - 1);
    }
    char url[256];
    if (BACKEND_API_KEY[0] != 0) {
        snprintf(url, sizeof(url), "%s?name=AWTAS_CONFIG.TXT&key=%s", base, BACKEND_API_KEY);
    } else {
        snprintf(url, sizeof(url), "%s?name=AWTAS_CONFIG.TXT", base);
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
    if (http_code < 200 || http_code >= 300) {
        Modem_PowerOff();
        return HAL_ERROR;
    }
    if (Modem_SendAT("AT+QHTTPREAD=60", "OK", 60000) != HAL_OK) {
        Modem_PowerOff();
        return HAL_ERROR;
    }
    char* body = strstr(modem_rx_buffer, "+QHTTPREAD:");
    if (body != NULL) {
        char* nl = strchr(body, '\n');
        char* cr = strchr(body, '\r');
        char* first = nl;
        if (first == NULL || (cr != NULL && cr < first)) {
            first = cr;
        }
        if (first != NULL) {
            body = first + 1;
        }
    } else {
        body = modem_rx_buffer;
    }
    size_t len = strlen(body);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out_buffer, body, len);
    out_buffer[len] = 0;
    Modem_PowerOff();
    return HAL_OK;
}
