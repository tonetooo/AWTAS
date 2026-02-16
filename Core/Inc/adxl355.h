#ifndef ADXL355_H
#define ADXL355_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

// ADXL355 Registers
#define ADXL355_DEVID_AD        0x00
#define ADXL355_DEVID_MST       0x01
#define ADXL355_PARTID          0x02
#define ADXL355_STATUS          0x04
#define ADXL355_FIFO_ENTRIES    0x05
#define ADXL355_TEMP2           0x06
#define ADXL355_TEMP1           0x07
#define ADXL355_XDATA3          0x08
#define ADXL355_XDATA2          0x09
#define ADXL355_XDATA1          0x0A
#define ADXL355_YDATA3          0x0B
#define ADXL355_YDATA2          0x0C
#define ADXL355_YDATA1          0x0D
#define ADXL355_ZDATA3          0x0E
#define ADXL355_ZDATA2          0x0F
#define ADXL355_ZDATA1          0x10
#define ADXL355_FIFO_DATA       0x11
#define ADXL355_ACT_EN          0x24
#define ADXL355_ACT_THRESH_H    0x25
#define ADXL355_ACT_THRESH_L    0x26
#define ADXL355_ACT_COUNT       0x27
#define ADXL355_FILTER          0x28
#define ADXL355_FIFO_SAMPLES    0x29
#define ADXL355_INT_MAP         0x2A
#define ADXL355_RANGE           0x2C
#define ADXL355_POWER_CTL       0x2D
#define ADXL355_SELF_TEST       0x2E
#define ADXL355_RESET           0x2F

// Data Structure
typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
    float x_g;
    float y_g;
    float z_g;
    uint32_t timestamp;
} ADXL355_Data_t;

// Range Definitions
typedef enum {
    ADXL355_RANGE_2G = 0x01,
    ADXL355_RANGE_4G = 0x02,
    ADXL355_RANGE_8G = 0x03
} ADXL355_Range_t;

// ODR Definitions
typedef enum {
    ADXL355_ODR_4000HZ = 0x00,
    ADXL355_ODR_2000HZ = 0x01,
    ADXL355_ODR_1000HZ = 0x02,
    ADXL355_ODR_500HZ  = 0x03,
    ADXL355_ODR_250HZ  = 0x04,
    ADXL355_ODR_125HZ  = 0x05,
    ADXL355_ODR_62_5HZ = 0x06,
    ADXL355_ODR_31_25HZ = 0x07
} ADXL355_ODR_t;

// Functions
void ADXL355_Write_Reg(uint8_t reg, uint8_t value);
uint8_t ADXL355_Read_Reg(uint8_t reg);
uint8_t ADXL355_Init(SPI_HandleTypeDef *hspi);
void ADXL355_Set_Range(ADXL355_Range_t range);
void ADXL355_Set_ODR(ADXL355_ODR_t odr);
void ADXL355_Config_Activity_Int(uint16_t threshold, uint8_t count);
void ADXL355_Config_WakeOnMotion(float threshold_g, uint8_t count);
uint8_t ADXL355_Read_Status(void);
uint8_t ADXL355_Get_FIFO_Entries(void);
void ADXL355_Config_FIFO(uint8_t samples);
void ADXL355_Read_Data(ADXL355_Data_t *data);
void ADXL355_Read_FIFO_Data(ADXL355_Data_t *data);
void ADXL355_Set_HPF(uint8_t enable);
void ADXL355_Read_FIFO(ADXL355_Data_t *buffer, uint8_t count);
int ADXL355_Read_FIFO_Burst(SPI_HandleTypeDef *hspi, ADXL355_Data_t *buffer, uint8_t count);

#endif
