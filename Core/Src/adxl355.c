#include "adxl355.h"
#include "main.h"
#include <stdio.h>

// Global handle for SPI (must be defined in main.c)
static SPI_HandleTypeDef *adxl_hspi;
static ADXL355_Range_t current_range = ADXL355_RANGE_2G;
static float current_sensitivity = 256000.0f; // Default for 2g (approx)

// Internal helpers
void ADXL355_Write_Reg(uint8_t reg, uint8_t value) {
    uint8_t data[2];
    data[0] = (reg << 1) & 0xFE; // Write bit = 0
    data[1] = value;
    
    HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(adxl_hspi, data, 2, 100);
    HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_SET);
}

uint8_t ADXL355_Read_Reg(uint8_t reg) {
    uint8_t tx_data = (reg << 1) | 0x01; // Read bit = 1
    uint8_t rx_data = 0;
    
    HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(adxl_hspi, &tx_data, 1, 100);
    HAL_SPI_Receive(adxl_hspi, &rx_data, 1, 100);
    HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_SET);
    
    return rx_data;
}

uint8_t ADXL355_Read_Status(void) {
    return ADXL355_Read_Reg(ADXL355_STATUS);
}

uint8_t ADXL355_Init(SPI_HandleTypeDef *hspi) {
    adxl_hspi = hspi;
    // 1. Reset Sensor
    ADXL355_Write_Reg(ADXL355_RESET, 0x52);
    HAL_Delay(100);
    
    // 2. Check Device IDs
    uint8_t devid_ad = 0;
    uint8_t devid_mst = 0;
    uint8_t partid = 0;

    for(int i=0; i<5; i++) {
        devid_ad = ADXL355_Read_Reg(ADXL355_DEVID_AD);
        devid_mst = ADXL355_Read_Reg(ADXL355_DEVID_MST);
        partid = ADXL355_Read_Reg(ADXL355_PARTID);
        
        // Check if we have the correct IDs
        // ADXL355: AD=0xAD, MST=0x1D, PARTID=0xED
        if(devid_ad == 0xAD && devid_mst == 0x1D && partid == 0xED) break;
        
        HAL_Delay(10);
    }

    if (devid_ad != 0xAD || devid_mst != 0x1D || partid != 0xED) {
        printf("Error: IDs mismatch. AD:0x%02X(Exp:0xAD), MST:0x%02X(Exp:0x1D), PART:0x%02X(Exp:0xED)\r\n", devid_ad, devid_mst, partid);
        return 0; // Error
    }
    
    // 3. Configure Filter (Default ODR = 125Hz -> 0x05)
    ADXL355_Write_Reg(ADXL355_FILTER, 0x05);
    
    // 4. Power Control (Measurement Mode)
    // Standby = 0 (Bit 0) -> Measurement Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
    
    // 5. Configure Interrupt Polarity (Active High for Rising Edge)
    // Read Range register, Set Bit 6 (INT_POL) = 1
    // Range bits (1:0) default is usually 01 (2g). 
    // We should preserve existing range settings.
    uint8_t range_reg = ADXL355_Read_Reg(ADXL355_RANGE);
    range_reg |= 0x40; // Set Bit 6
    ADXL355_Write_Reg(ADXL355_RANGE, range_reg);

    // Set default range to 2g explicitly
    ADXL355_Set_Range(ADXL355_RANGE_2G);

    return 1; // Success
}

void ADXL355_Set_Range(ADXL355_Range_t range) {
    // 1. Enter Standby Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x01);

    // 2. Configure Range
    // Bits 1:0 determine range.
    // 01 = +/- 2g
    // 10 = +/- 4g
    // 11 = +/- 8g
    uint8_t range_reg = ADXL355_Read_Reg(ADXL355_RANGE);
    range_reg &= 0xFC; // Clear bits 1:0
    range_reg |= (range & 0x03);
    ADXL355_Write_Reg(ADXL355_RANGE, range_reg);

    // 3. Update internal sensitivity
    current_range = range;
    switch(range) {
        case ADXL355_RANGE_2G:
            current_sensitivity = 256000.0f; // ~3.9ug/LSB
            break;
        case ADXL355_RANGE_4G:
            current_sensitivity = 128000.0f; // ~7.8ug/LSB
            break;
        case ADXL355_RANGE_8G:
            current_sensitivity = 64000.0f;  // ~15.6ug/LSB
            break;
        default:
            current_sensitivity = 256000.0f;
            break;
    }

    // 4. Return to Measurement Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
}

void ADXL355_Set_ODR(ADXL355_ODR_t odr) {
    // 1. Enter Standby Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x01);

    // 2. Configure ODR
    // Read current filter register to preserve High Pass settings if any (assuming bits 7:4 are 0 for now)
    // Only updating ODR (bits 3:0)
    uint8_t filter_reg = ADXL355_Read_Reg(ADXL355_FILTER);
    filter_reg &= 0xF0; // Clear lower 4 bits
    filter_reg |= (odr & 0x0F);
    ADXL355_Write_Reg(ADXL355_FILTER, filter_reg);

    // 3. Return to Measurement Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
}

void ADXL355_Set_HPF(uint8_t enable) {
    // 1. Enter Standby Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x01);
    
    // 2. Configure HPF
    // Bits 6:4 determine corner frequency.
    // 000 = OFF
    // 001 = 24.7e-4 * ODR (Corner freq)
    uint8_t filter_reg = ADXL355_Read_Reg(ADXL355_FILTER);
    filter_reg &= 0x8F; // Clear bits 6:4 (HPF)
    
    if (enable) {
        // Set to 001 (Basic HPF)
        filter_reg |= (0x01 << 4);
    }
    
    ADXL355_Write_Reg(ADXL355_FILTER, filter_reg);
    
    // 3. Return to Measurement Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
}

void ADXL355_Config_Activity_Int(uint16_t threshold, uint8_t count) {
    // 1. Enter Standby Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x01);

    // 2. Set Activity Threshold
    ADXL355_Write_Reg(ADXL355_ACT_THRESH_H, (uint8_t)(threshold >> 8));
    ADXL355_Write_Reg(ADXL355_ACT_THRESH_L, (uint8_t)(threshold & 0xFF));
    
    // 3. Set Activity Count
    ADXL355_Write_Reg(ADXL355_ACT_COUNT, count);
    
    // 4. Map to INT1 (Clear bit 4 of INT_MAP -> Activity)
    uint8_t int_map = ADXL355_Read_Reg(ADXL355_INT_MAP);
    int_map &= ~0x10; 
    ADXL355_Write_Reg(ADXL355_INT_MAP, int_map);
    
    // 5. Enable Activity Detection
    ADXL355_Write_Reg(ADXL355_ACT_EN, 0x01);

    // 6. Return to Measurement Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
}

void ADXL355_Config_WakeOnMotion(float threshold_g, uint8_t count) {
    // 1. Enter Standby Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x01);

    // ADXL355 Activity threshold registers (0x25, 0x26) are compared to the 16 MSBs of the acceleration data.
    // For 2g range, sensitivity is 256,000 LSB/g (20-bit). 
    // The 16 MSBs comparison means 1g = 256,000 / 16 = 16,000.
    uint16_t threshold_val = (uint16_t)(threshold_g * (current_sensitivity / 16.0f)); 
    
    // 2. Set Activity Threshold
    ADXL355_Write_Reg(ADXL355_ACT_THRESH_H, (uint8_t)(threshold_val >> 8));
    ADXL355_Write_Reg(ADXL355_ACT_THRESH_L, (uint8_t)(threshold_val & 0xFF));
    
    // 3. Set Activity Count (Number of consecutive samples above threshold)
    ADXL355_Write_Reg(ADXL355_ACT_COUNT, count);
    
    // 4. Enable Axes (X, Y) in ACT_EN (0x24)
    // We disable Z (bit 2) to avoid constant triggering from gravity (1G).
    ADXL355_Write_Reg(ADXL355_ACT_EN, 0x03); 
    
    // 5. Map Activity Interrupt to INT1
    // Register 0x2A (INT_MAP): Bit 3 is ACT_EN1 (1 = Activity to INT1)
    uint8_t int_map = ADXL355_Read_Reg(ADXL355_INT_MAP);
    int_map |= 0x08; // Set bit 3 to enable Activity on INT1
    ADXL355_Write_Reg(ADXL355_INT_MAP, int_map);

    // 6. Return to Measurement Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
}

void ADXL355_Read_Data(ADXL355_Data_t *data) {
    uint8_t tx_data = (ADXL355_XDATA3 << 1) | 0x01;
    uint8_t raw_data[9];
    
    HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(adxl_hspi, &tx_data, 1, 100);
    HAL_SPI_Receive(adxl_hspi, raw_data, 9, 100);
    HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_SET);
    
    // Convert 20-bit data to 32-bit signed integer
    int32_t x = ((int32_t)raw_data[0] << 12) | ((int32_t)raw_data[1] << 4) | ((int32_t)raw_data[2] >> 4);
    int32_t y = ((int32_t)raw_data[3] << 12) | ((int32_t)raw_data[4] << 4) | ((int32_t)raw_data[5] >> 4);
    int32_t z = ((int32_t)raw_data[6] << 12) | ((int32_t)raw_data[7] << 4) | ((int32_t)raw_data[8] >> 4);
    
    // Sign extension for 20-bit numbers
    if (x & 0x80000) x |= 0xFFF00000;
    if (y & 0x80000) y |= 0xFFF00000;
    if (z & 0x80000) z |= 0xFFF00000;
    
    data->x = x;
    data->y = y;
    data->z = z;
    
    // Calculate G values
    data->x_g = (float)x / current_sensitivity;
    data->y_g = (float)y / current_sensitivity;
    data->z_g = (float)z / current_sensitivity;
    
    data->timestamp = HAL_GetTick();
}

uint8_t ADXL355_Get_FIFO_Entries(void) {
    return ADXL355_Read_Reg(ADXL355_FIFO_ENTRIES);
}

void ADXL355_Config_FIFO(uint8_t samples) {
    // Must be in Standby
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x01);

    // Configure FIFO in Stream Mode (0x01)
    // Stream Mode: FIFO collects data; when full, oldest data is overwritten.
    // Store X,Y,Z data (implicit default if not specified otherwise, but we just set mode)
    
    // Set FIFO Samples (e.g., 96 samples = 0x60)
    ADXL355_Write_Reg(ADXL355_FIFO_SAMPLES, samples); 
    
    // Restore Measurement Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
}

void ADXL355_Read_FIFO_Data(ADXL355_Data_t *data) {
    uint8_t tx_data = (ADXL355_FIFO_DATA << 1) | 0x01;
    uint8_t raw_data[9];
    
    HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(adxl_hspi, &tx_data, 1, 100);
    HAL_SPI_Receive(adxl_hspi, raw_data, 9, 100);
    HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_SET);
    
    // Check marker bits (Bit 0 of byte 2, 5, 8 indicates empty/special)
    // For now assume valid X,Y,Z data
    
    int32_t x = ((int32_t)raw_data[0] << 12) | ((int32_t)raw_data[1] << 4) | ((int32_t)raw_data[2] >> 4);
    int32_t y = ((int32_t)raw_data[3] << 12) | ((int32_t)raw_data[4] << 4) | ((int32_t)raw_data[5] >> 4);
    int32_t z = ((int32_t)raw_data[6] << 12) | ((int32_t)raw_data[7] << 4) | ((int32_t)raw_data[8] >> 4);
    
    if (x & 0x80000) x |= 0xFFF00000;
    if (y & 0x80000) y |= 0xFFF00000;
    if (z & 0x80000) z |= 0xFFF00000;
    
    data->x = x;
    data->y = y;
    data->z = z;
}

void ADXL355_Read_FIFO(ADXL355_Data_t *buffer, uint8_t count) {
    // This implementation is slow (toggle CS every sample), but safe.
    // For burst read, we should keep CS low, but let's stick to simple first.
    // Actually, reading FIFO_DATA multiple times in one transaction is better.
    // But let's reuse the single sample read for simplicity if performance allows.
    // However, to read FIFO correctly, we just read from 0x11 repeatedly.
    
    for(int i=0; i<count; i++) {
        ADXL355_Read_FIFO_Data(&buffer[i]);
    }
}

int ADXL355_Read_FIFO_Burst(SPI_HandleTypeDef *hspi, ADXL355_Data_t *buffer, uint8_t count) {
    for(int i=0; i<count; i++) {
        ADXL355_Read_Data(&buffer[i]);
    }
    return count;
}
