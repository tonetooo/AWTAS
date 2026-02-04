#ifndef UART_INTERFACE_H
#define UART_INTERFACE_H

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdint.h>

// Functions
void UART_Print(const char *msg);
void UART_Print_Data(int32_t x, int32_t y, int32_t z, uint32_t timestamp);
uint8_t UART_Check_Command(char *cmd);

#endif
