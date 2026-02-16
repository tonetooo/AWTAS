/*
 * SD Card Diskio Driver for FATFS
 * Links SD SPI functions to FATFS
 */

#include "../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.h"
#include "Sd_spi.h"
#include <string.h>

/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

/* ============================= */
/* DISKIO FUNCTIONS             */
/* ============================= */

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_initialize(BYTE pdrv)
{
    Stat = STA_NOINIT;

    if (sd_init() == 0)
    {
        Stat &= ~STA_NOINIT;
    }

    return Stat;
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_status(BYTE pdrv)
{
    return Stat;
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT SD_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (Stat & STA_NOINIT)
        return RES_NOTRDY;

    if (count == 1)
    {
        if (sd_read_block(buff, sector) == 0)
            return RES_OK;
    }
    else
    {
        if (sd_read_blocks(buff, sector, count) == 0)
            return RES_OK;
    }

    return RES_ERROR;
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT SD_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (Stat & STA_NOINIT)
        return RES_NOTRDY;

    if (count == 1)
    {
        if (sd_write_block(buff, sector) == 0)
            return RES_OK;
    }
    else
    {
        if (sd_write_blocks(buff, sector, count) == 0)
            return RES_OK;
    }

    return RES_ERROR;
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    DRESULT res = RES_ERROR;

    if (Stat & STA_NOINIT)
        return RES_NOTRDY;

    switch (cmd)
    {
        /* Make sure that no pending write process */
        case CTRL_SYNC:
            res = RES_OK;
            break;

        /* Get number of sectors on the disk (DWORD) */
        case GET_SECTOR_COUNT:
            *(DWORD*)buff = 1024000;  // Dummy 500MB
            res = RES_OK;
            break;

        /* Get R/W sector size (WORD) */
        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            res = RES_OK;
            break;

        /* Get erase block size in unit of sector (DWORD) */
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 8;  // 4KB erase block
            res = RES_OK;
            break;

        default:
            res = RES_PARERR;
    }

    return res;
}
#endif /* _USE_IOCTL == 1 */

/**
  * @brief  Gets current time for file timestamps
  * @retval DWORD: Packed date/time
  */
__weak DWORD get_fattime(void)
{
    /* Return a fixed date/time: 2025-01-01 00:00:00 */
    return ((DWORD)(2025 - 1980) << 25)  /* Year */
         | ((DWORD)1 << 21)              /* Month */
         | ((DWORD)1 << 16)              /* Day */
         | ((DWORD)0 << 11)              /* Hour */
         | ((DWORD)0 << 5)               /* Minute */
         | ((DWORD)0 >> 1);              /* Second / 2 */
}

/* ============================= */
/* DISKIO DRIVER STRUCTURE      */
/* ============================= */

const Diskio_drvTypeDef SD_Driver =
{
    SD_initialize,
    SD_status,
    SD_read,
#if _USE_WRITE == 1
    SD_write,
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
    SD_ioctl,
#endif /* _USE_IOCTL == 1 */
};
