#include "uart_interface.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

extern UART_HandleTypeDef huart2; // Console UART

void UART_Print(const char *msg) {
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
}

void UART_Print_Data(int32_t x, int32_t y, int32_t z, uint32_t timestamp) {
    char buffer[64];
    // Format: Timestamp, X, Y, Z
    snprintf(buffer, sizeof(buffer), "%lu,%ld,%ld,%ld\r\n", timestamp, x, y, z);
    HAL_UART_Transmit(&huart2, (uint8_t*)buffer, strlen(buffer), 50);
}

uint8_t UART_Check_Command(char *cmd) {
    if (HAL_UART_Receive(&huart2, (uint8_t*)cmd, 1, 0) == HAL_OK) {
        return 1; // Command received
    }
    return 0; // No command
}
