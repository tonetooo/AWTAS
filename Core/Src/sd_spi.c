#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "Sd_spi.h"
#include "ff_gen_drv.h"

/***************************************************************
 * 🔧 USER CONFIGURATION - MODIFY THIS FOR YOUR BOARD
 ***************************************************************/
#define USE_DMA 0  // Set to 1 for DMA, 0 for polling

extern SPI_HandleTypeDef hspi1;
#define SD_SPI_HANDLE hspi1

// CS Pin - Change SD_CS to match your CubeMX label
#define SD_CS_LOW()     HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET)
#define SD_CS_HIGH()    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET)

/***************************************************************
 * 🚫 DO NOT MODIFY BELOW THIS LINE
 ***************************************************************/

// FATFS Variables
static FATFS fs;
static FIL fil;
static char sd_path[4] = "0:/";
static uint8_t sd_card_type = SD_TYPE_UNKNOWN;

// SPI Transfer Functions
static uint8_t spi_send(uint8_t data)
{
    uint8_t rx_data;
#if USE_DMA
    HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &data, &rx_data, 1, 1000);
#else
    HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &data, &rx_data, 1, 1000);
#endif
    return rx_data;
}

static void spi_send_dummy(uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        spi_send(0xFF);
    }
}

// Wait for card ready
static uint8_t sd_wait_ready(void)
{
    uint8_t res;
    uint16_t retry = 0;
    do {
        res = spi_send(0xFF);
        retry++;
    } while (res != 0xFF && retry < 500);
    return (res == 0xFF) ? 0 : 1;
}

// SD Card Command Functions
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t res, retry = 0;

    if (cmd != CMD0) {
        if (sd_wait_ready() != 0) return 0xFF;
    }

    // Send command packet
    spi_send(0x40 | cmd);
    spi_send((uint8_t)(arg >> 24));
    spi_send((uint8_t)(arg >> 16));
    spi_send((uint8_t)(arg >> 8));
    spi_send((uint8_t)arg);

    // CRC
    uint8_t crc = 0xFF;
    if (cmd == CMD0) crc = 0x95;
    else if (cmd == CMD8) crc = 0x87;
    spi_send(crc);

    // Wait for response
    retry = 0;
    do {
        res = spi_send(0xFF);
        retry++;
    } while ((res & 0x80) && retry < 255);

    return res;
}

// SD Card Initialization
uint8_t sd_init(void)
{
    uint8_t res, retry = 0;

    printf("[STORAGE] sd_init starting...\r\n");
    HAL_Delay(150); // Give SD card more time to stabilize

    // Slow down SPI for initialization (< 400kHz)
    // STM32F446 SPI1 is on APB2 (90MHz max). 90/256 = 351kHz.
    SD_SPI_HANDLE.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    if (HAL_SPI_Init(&SD_SPI_HANDLE) != HAL_OK) {
        printf("[STORAGE] HAL_SPI_Init failed\r\n");
        return 1;
    }

    SD_CS_HIGH();
    // Send 100+ dummy clocks with CS high to enter SPI mode
    for(int i=0; i<20; i++) {
        spi_send(0xFF);
    }

    // CMD0: GO_IDLE_STATE
    retry = 0;
    do {
        SD_CS_HIGH();
        spi_send(0xFF); // Sync clocks
        
        SD_CS_LOW();
        res = sd_send_cmd(CMD0, 0);
        SD_CS_HIGH(); // Must toggle CS for CMD0 retries on some cards
        
        if (res == 0x01) {
            break;
        }
        
        spi_send(0xFF);
        HAL_Delay(10); // Short delay
        retry++;
    } while (retry < 100);

    if (res != 0x01) {
        // Try one more sequence with more sync clocks
        printf("[STORAGE] CMD0 retry sequence...\r\n");
        SD_CS_HIGH();
        for(int i=0; i<100; i++) spi_send(0xFF);
        
        retry = 0;
        do {
            SD_CS_LOW();
            res = sd_send_cmd(CMD0, 0);
            SD_CS_HIGH();
            if (res == 0x01) break;
            HAL_Delay(50);
            retry++;
        } while (retry < 150);
    }

    if (res != 0x01) {
        printf("[STORAGE] SD Init Failed: CMD0 (Res=0x%02X after %d retries)\r\n", res, retry);
        SD_CS_HIGH();
        return 1;
    }

    printf("[STORAGE] CMD0 Success (0x%02X) at retry %d\r\n", res, retry);

    // CMD8: SEND_IF_COND (Required for SDHC/SDXC)
    SD_CS_LOW();
    res = sd_send_cmd(CMD8, 0x1AA);
    if (res == 0x01) {
        // V2.0 Card
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = spi_send(0xFF);
        SD_CS_HIGH();
        spi_send(0xFF);

        printf("[STORAGE] V2.0 Card detected, OCR: %02X %02X %02X %02X\r\n", ocr[0], ocr[1], ocr[2], ocr[3]);

        if (ocr[2] == 0x01 && ocr[3] == 0xAA) {
            // Valid V2.0 card
            retry = 0;
            do {
                SD_CS_LOW();
                sd_send_cmd(CMD55, 0);
                SD_CS_HIGH();
                spi_send(0xFF);

                SD_CS_LOW();
                res = sd_send_cmd(ACMD41, 0x40000000);
                SD_CS_HIGH();
                spi_send(0xFF);

                retry++;
                HAL_Delay(10);
            } while (res != 0x00 && retry < 255);

            if (res == 0x00) {
                // Check CCS (Capacity Status) in OCR
                SD_CS_LOW();
                if (sd_send_cmd(CMD58, 0) == 0x00) {
                    for (int i = 0; i < 4; i++) ocr[i] = spi_send(0xFF);
                    sd_card_type = (ocr[0] & 0x40) ? SD_TYPE_V2HC : SD_TYPE_V2;
                }
                SD_CS_HIGH();
                spi_send(0xFF);
            }
        }
    } else {
        SD_CS_HIGH();
        spi_send(0xFF);
        // V1.x Card or MMC
        printf("[STORAGE] V1.x/MMC Card detected\r\n");
        retry = 0;
        do {
            SD_CS_LOW();
            sd_send_cmd(CMD55, 0);
            SD_CS_HIGH();
            spi_send(0xFF);

            SD_CS_LOW();
            res = sd_send_cmd(ACMD41, 0);
            SD_CS_HIGH();
            spi_send(0xFF);

            if (res == 0x00) {
                sd_card_type = SD_TYPE_V1;
                break;
            }

            SD_CS_LOW();
            res = sd_send_cmd(CMD1, 0);
            SD_CS_HIGH();
            spi_send(0xFF);

            if (res == 0x00) {
                sd_card_type = SD_TYPE_MMC;
                break;
            }
            retry++;
            HAL_Delay(10);
        } while (retry < 255);
    }

    if (sd_card_type == SD_TYPE_UNKNOWN) {
        printf("[STORAGE] SD Init Failed: Unknown card type (Res=0x%02X)\r\n", res);
        SD_CS_HIGH();
        return 1;
    }

    // Set block size to 512 bytes (for non-SDHC cards)
    if (sd_card_type != SD_TYPE_V2HC) {
        sd_send_cmd(CMD16, 512);
    }

    SD_CS_HIGH();
    spi_send(0xFF);

    printf("[STORAGE] SD Card Initialized. Type: %d\r\n", sd_card_type);

    // Speed up SPI for data transfer
    // 90MHz / 8 = 11.25MHz (safe for most SD cards)
    SD_SPI_HANDLE.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; 
    if (HAL_SPI_Init(&SD_SPI_HANDLE) != HAL_OK) {
        printf("[STORAGE] Final SPI Init failed\r\n");
    }

    return 0;
}

// Read Single Block
uint8_t sd_read_block(uint8_t *buf, uint32_t sector)
{
    uint8_t res;
    uint16_t retry = 0;

    SD_CS_LOW();

    // For SDHC, sector = block number; for SD, sector = byte address
    if (sd_card_type != SD_TYPE_V2HC) {
        sector *= 512;
    }

    res = sd_send_cmd(CMD17, sector);
    if (res != 0x00) {
        SD_CS_HIGH();
        return 1;
    }

    // Wait for data token
    do {
        res = spi_send(0xFF);
        retry++;
    } while (res != 0xFE && retry < 5000);

    if (res != 0xFE) {
        SD_CS_HIGH();
        return 2;
    }

    // Read 512 bytes
#if USE_DMA
    HAL_SPI_Receive_DMA(&SD_SPI_HANDLE, buf, 512);
    while (HAL_SPI_GetState(&SD_SPI_HANDLE) != HAL_SPI_STATE_READY);
#else
    for (int i = 0; i < 512; i++) {
        buf[i] = spi_send(0xFF);
    }
#endif

    // Read CRC (2 bytes)
    spi_send(0xFF);
    spi_send(0xFF);

    SD_CS_HIGH();
    spi_send(0xFF);

    return 0;
}

// Write Single Block
uint8_t sd_write_block(const uint8_t *buf, uint32_t sector)
{
    uint8_t res;
    uint16_t retry = 0;

    SD_CS_LOW();

    if (sd_card_type != SD_TYPE_V2HC) {
        sector *= 512;
    }

    res = sd_send_cmd(CMD24, sector);
    if (res != 0x00) {
        SD_CS_HIGH();
        return 1;
    }

    spi_send(0xFF);
    spi_send(0xFE); // Data token

    // Write 512 bytes
#if USE_DMA
    HAL_SPI_Transmit_DMA(&SD_SPI_HANDLE, (uint8_t*)buf, 512);
    while (HAL_SPI_GetState(&SD_SPI_HANDLE) != HAL_SPI_STATE_READY);
#else
    for (int i = 0; i < 512; i++) {
        spi_send(buf[i]);
    }
#endif

    // Dummy CRC
    spi_send(0xFF);
    spi_send(0xFF);

    // Wait for response
    res = spi_send(0xFF);
    if ((res & 0x1F) != 0x05) {
        SD_CS_HIGH();
        return 2;
    }

    // Wait for card to finish
    retry = 0;
    do {
        res = spi_send(0xFF);
        retry++;
    } while (res == 0x00 && retry < 50000);

    SD_CS_HIGH();
    spi_send(0xFF);

    return 0;
}

// Multiple block read/write
uint8_t sd_read_blocks(uint8_t *buf, uint32_t sector, uint32_t count)
{
    uint8_t res;
    if (count == 1) return sd_read_block(buf, sector);

    SD_CS_LOW();
    if (sd_card_type != SD_TYPE_V2HC) sector *= 512;

    res = sd_send_cmd(CMD18, sector); // READ_MULTIPLE_BLOCK
    if (res != 0x00) {
        SD_CS_HIGH();
        return 1;
    }

    do {
        uint16_t retry = 0;
        while (spi_send(0xFF) != 0xFE && retry < 5000) retry++;
        if (retry >= 5000) break;

        for (int i = 0; i < 512; i++) *buf++ = spi_send(0xFF);
        spi_send(0xFF); // CRC
        spi_send(0xFF);
    } while (--count);

    sd_send_cmd(CMD12, 0); // STOP_TRANSMISSION
    SD_CS_HIGH();
    spi_send(0xFF);

    return (count == 0) ? 0 : 1;
}

uint8_t sd_write_blocks(const uint8_t *buf, uint32_t sector, uint32_t count)
{
    uint8_t res;
    if (count == 1) return sd_write_block(buf, sector);

    SD_CS_LOW();
    if (sd_card_type != SD_TYPE_V2HC) sector *= 512;

    res = sd_send_cmd(CMD25, sector); // WRITE_MULTIPLE_BLOCK
    if (res != 0x00) {
        SD_CS_HIGH();
        return 1;
    }

    do {
        spi_send(0xFF);
        spi_send(0xFC); // Multi-block data token
        for (int i = 0; i < 512; i++) spi_send(*buf++);
        spi_send(0xFF); // CRC
        spi_send(0xFF);
        
        res = spi_send(0xFF);
        if ((res & 0x1F) != 0x05) break;
        
        uint32_t retry = 0;
        while (spi_send(0xFF) == 0x00 && retry < 50000) retry++;
        if (retry >= 50000) break;
    } while (--count);

    spi_send(0xFD); // Stop token
    uint32_t retry = 0;
    while (spi_send(0xFF) == 0x00 && retry < 50000) retry++;

    SD_CS_HIGH();
    spi_send(0xFF);

    return (count == 0) ? 0 : 1;
}

/***************************************************************
 * HIGH-LEVEL FILE OPERATIONS
 ***************************************************************/

// Mount SD Card
int sd_mount(void)
{
    FRESULT res;

    printf("[STORAGE] Calling sd_init...\r\n");
    if (sd_init() != 0) {
        printf("[STORAGE] SD Card Init Failed\r\n");
        return -1;
    }

    printf("[STORAGE] SD Init OK, linking driver...\r\n");

    // Link the SD driver to FATFS
    extern const Diskio_drvTypeDef USER_Driver;
    if (FATFS_LinkDriver(&USER_Driver, sd_path) != 0)
    {
        printf("[STORAGE] Failed to link SD driver\r\n");
        return -1;
    }

    printf("[STORAGE] Driver linked, calling f_mount...\r\n");

    res = f_mount(&fs, sd_path, 1);

    if (res != FR_OK) {
        printf("[STORAGE] Mount failed (%d), trying to format...\r\n", res);

        // Try to format the card
        BYTE work[4096];
        res = f_mkfs(sd_path, FM_FAT32, 0, work, sizeof(work));

        if (res == FR_OK) {
            printf("[STORAGE] Card formatted, mounting again...\r\n");
            res = f_mount(&fs, sd_path, 1);
        }
    }

    if (res != FR_OK) {
        printf("[STORAGE] Mount Failed: %d\r\n", res);
        FATFS_UnLinkDriver(sd_path);
        return -1;
    }

    printf("[STORAGE] SD Card Mounted\r\n");
    return 0;
}

// Unmount SD Card
void sd_unmount(void)
{
    f_mount(NULL, sd_path, 0);
    printf("[STORAGE] SD Card Unmounted\r\n");
}

// Write/Create File
int sd_write_file(const char *filename, const char *data)
{
    printf("[STORAGE] Trying to open file: %s\r\n", filename);
    printf("[STORAGE] FATFS mounted at: %s\r\n", sd_path);

    FRESULT res = f_open(&fil, filename, FA_CREATE_ALWAYS | FA_WRITE);

    printf("[STORAGE] f_open returned: %d\r\n", res);

    if (res != FR_OK) {
        printf("[STORAGE] Open Failed: %d\r\n", res);

        // Try with full path
        char fullpath[50];
        sprintf(fullpath, "%swater_log.csv", sd_path);
        printf("[STORAGE] Trying full path: %s\r\n", fullpath);
        res = f_open(&fil, fullpath, FA_CREATE_ALWAYS | FA_WRITE);
        printf("[STORAGE] f_open with full path returned: %d\r\n", res);

        if (res != FR_OK)
            return -1;
    }

    UINT bw;
    res = f_write(&fil, data, strlen(data), &bw);
    f_close(&fil);

    if (res == FR_OK) {
        printf("[STORAGE] Written %u bytes to %s\r\n", bw, filename);
        return 0;
    }
    return -1;
}

// Read File
int sd_read_file(const char *filename, char *buffer, uint32_t size, UINT *bytes_read)
{
    FRESULT res = f_open(&fil, filename, FA_READ);
    if (res != FR_OK) {
        printf("[STORAGE] Read Failed: %d\r\n", res);
        return -1;
    }

    res = f_read(&fil, buffer, size - 1, bytes_read);
    buffer[*bytes_read] = '\0';
    f_close(&fil);

    return (res == FR_OK) ? 0 : -1;
}

// Append to File
int sd_append_file(const char *filename, const char *data)
{
    FRESULT res = f_open(&fil, filename, FA_OPEN_APPEND | FA_WRITE);
    if (res != FR_OK) {
        printf("[STORAGE] Append Failed: %d\r\n", res);
        return -1;
    }

    UINT bw;
    res = f_write(&fil, data, strlen(data), &bw);
    f_close(&fil);

    return (res == FR_OK) ? 0 : -1;
}

// List Files
void sd_list_files(void)
{
    DIR dir;
    FILINFO fno;

    if (f_opendir(&dir, "/") == FR_OK) {
        printf("\n[STORAGE] Files on SD Card:\r\n");
        printf("---------------------------\r\n");
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
            printf("  %s (%lu bytes)\r\n", fno.fname, fno.fsize);
        }
        printf("---------------------------\r\n");
        f_closedir(&dir);
    }
}
