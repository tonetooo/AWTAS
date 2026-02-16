#ifndef SD_SPI_H
#define SD_SPI_H

#include "stm32f4xx_hal.h"
#include "ff.h"

// SD Card Commands
#define CMD0    0    // GO_IDLE_STATE
#define CMD1    1    // SEND_OP_COND
#define CMD8    8    // SEND_IF_COND
#define CMD9    9    // SEND_CSD
#define CMD10   10   // SEND_CID
#define CMD12   12   // STOP_TRANSMISSION
#define CMD16   16   // SET_BLOCKLEN
#define CMD17   17   // READ_SINGLE_BLOCK
#define CMD18   18   // READ_MULTIPLE_BLOCK
#define CMD23   23   // SET_BLOCK_COUNT
#define CMD24   24   // WRITE_BLOCK
#define CMD25   25   // WRITE_MULTIPLE_BLOCK
#define CMD55   55   // APP_CMD
#define CMD58   58   // READ_OCR
#define ACMD41  41   // SD_SEND_OP_COND

// SD Card Types
#define SD_TYPE_UNKNOWN  0
#define SD_TYPE_V1       1
#define SD_TYPE_V2       2
#define SD_TYPE_V2HC     3
#define SD_TYPE_MMC      4

// Function Prototypes
uint8_t sd_init(void);
uint8_t sd_read_block(uint8_t *buf, uint32_t sector);
uint8_t sd_write_block(const uint8_t *buf, uint32_t sector);
uint8_t sd_read_blocks(uint8_t *buf, uint32_t sector, uint32_t count);
uint8_t sd_write_blocks(const uint8_t *buf, uint32_t sector, uint32_t count);

// High-level file operations
int sd_mount(void);
void sd_unmount(void);
int sd_write_file(const char *filename, const char *data);
int sd_read_file(const char *filename, char *buffer, uint32_t size, UINT *bytes_read);
int sd_append_file(const char *filename, const char *data);
void sd_list_files(void);

#endif // SD_SPI_H
