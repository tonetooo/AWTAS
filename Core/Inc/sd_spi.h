#ifndef __SD_SPI_H
#define __SD_SPI_H

#include "stm32f4xx_hal.h"
#include "diskio.h"

/* SD Commands */
#define CMD0    (0)     /* GO_IDLE_STATE */
#define CMD1    (1)     /* SEND_OP_COND (MMC) */
#define ACMD41  (0x80+41) /* SEND_OP_COND (SDC) */
#define CMD8    (8)     /* SEND_IF_COND */
#define CMD9    (9)     /* SEND_CSD */
#define CMD10   (10)    /* SEND_CID */
#define CMD12   (12)    /* STOP_TRANSMISSION */
#define ACMD13  (0x80+13) /* SD_STATUS (SDC) */
#define CMD16   (16)    /* SET_BLOCKLEN */
#define CMD17   (17)    /* READ_SINGLE_BLOCK */
#define CMD18   (18)    /* READ_MULTIPLE_BLOCK */
#define CMD23   (23)    /* SET_BLOCK_COUNT (MMC) */
#define ACMD23  (0x80+23) /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24   (24)    /* WRITE_BLOCK */
#define CMD25   (25)    /* WRITE_MULTIPLE_BLOCK */
#define CMD55   (55)    /* APP_CMD */
#define CMD58   (58)    /* READ_OCR */

/* Card type flags (CardType) */
#define CT_MMC		0x01		/* MMC ver 3 */
#define CT_SD1		0x02		/* SD ver 1 */
#define CT_SD2		0x04		/* SD ver 2 */
#define CT_SDC		(CT_SD1|CT_SD2)	/* SD */
#define CT_BLOCK	0x08		/* Block addressing */

/* Functions */
DSTATUS SD_disk_initialize (void);
DSTATUS SD_disk_status (void);
DRESULT SD_disk_read (BYTE* buff, DWORD sector, UINT count);
DRESULT SD_disk_write (const BYTE* buff, DWORD sector, UINT count);
DRESULT SD_disk_ioctl (BYTE cmd, void* buff);

void SD_SPI_Init(SPI_HandleTypeDef* hspi);

#endif
