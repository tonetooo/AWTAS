#ifndef __QUECTEL_DRIVE_H
#define __QUECTEL_DRIVE_H

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

/* Configuración del Módem */
#define MODEM_BUFFER_SIZE 1024
#define HTTP_CHUNK_SIZE   1024

typedef enum {
    MODEM_STATE_OFF,
    MODEM_STATE_POWERING_ON,
    MODEM_STATE_READY,
    MODEM_STATE_NET_CONFIG,
    MODEM_STATE_HTTP_CFG,
    MODEM_STATE_UPLOADING,
    MODEM_STATE_ERROR
} ModemState_t;

/* Prototipos de funciones */
void Modem_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef Modem_PowerOn(void);
void Modem_PowerOff(void);
HAL_StatusTypeDef Modem_SendAT(char* command, char* expected_reply, uint32_t timeout);
HAL_StatusTypeDef Modem_CheckConnection(void);
HAL_StatusTypeDef Modem_UploadFile(const char* filename);
HAL_StatusTypeDef Modem_DownloadConfig(char* out_buffer, uint16_t out_size);

#endif /* __QUECTEL_DRIVE_H */
