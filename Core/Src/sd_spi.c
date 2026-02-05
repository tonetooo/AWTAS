#include "sd_spi.h"
#include "main.h"
#include "uart_interface.h" // Ensure UART_Print is visible

/* Definitions for MMC/SDC command are in sd_spi.h */

extern SPI_HandleTypeDef hspi2;
static volatile DSTATUS Stat = STA_NOINIT;
static BYTE CardType;

/* Forward declarations */
static void deselect (void);
static int wait_ready (void);
static BYTE xchg_spi (BYTE dat);
static void rcvr_spi_multi (BYTE *buff, UINT btr);
static void xmit_spi_multi (const BYTE *buff, UINT btx);

/*-----------------------------------------------------------------------*/
/* SPI Controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/

/* Select the card and wait for ready */
static int select (void)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
    xchg_spi(0xFF); /* Dummy clock (force DO enabled) */
    if (wait_ready()) return 1;
    deselect();
    return 0;
}

/* Deselect the card and release SPI bus */
static void deselect (void)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    xchg_spi(0xFF); /* Dummy clock (force DO hi-z for multiple slave SPI) */
}

/* Transmit/Receive a byte via SPI  */
static BYTE xchg_spi (BYTE dat)
{
    BYTE rx_dat;
    HAL_SPI_TransmitReceive(&hspi2, &dat, &rx_dat, 1, 100);
    return rx_dat;
}

/* Receive a data block fast */
static void rcvr_spi_multi (BYTE *buff, UINT btr)
{
    HAL_SPI_Receive(&hspi2, buff, btr, 1000);
}

/* Transmit a data block fast */
static void xmit_spi_multi (const BYTE *buff, UINT btx)
{
    HAL_SPI_Transmit(&hspi2, (BYTE*)buff, btx, 1000);
}


/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static int wait_ready (void)
{
    BYTE d;
    UINT tmr;

    for (tmr = 500; tmr; tmr--) {   /* Wait for ready in timeout of 500ms */
        d = xchg_spi(0xFF);
        if (d == 0xFF) return 1;
        HAL_Delay(1);
    }
    return 0;
}


/*-----------------------------------------------------------------------*/
/* CRC7 Calculation                                                      */
/*-----------------------------------------------------------------------*/
static BYTE get_crc7 (const BYTE* data, int len) {
    BYTE crc = 0;
    while (len--) {
        BYTE d = *data++;
        for (int i = 0; i < 8; i++) {
            crc <<= 1;
            if ((d ^ crc) & 0x80) crc ^= 0x09;
            d <<= 1;
        }
    }
    return (crc << 1) | 1;
}

/*-----------------------------------------------------------------------*/
/* Send a command packet to MMC                                          */
/*-----------------------------------------------------------------------*/

static BYTE send_cmd (
    BYTE cmd,       /* Command byte */
    DWORD arg       /* Argument */
)
{
    BYTE n, res;
    
    /* Select the card and wait for ready */
    deselect();
    if (!select()) return 0xFF;
    if (!wait_ready()) return 0xFF;

    /* ACMD<n> is the command sequence of CMD55-CMD<n> */
    if (cmd & 0x80) {
        cmd &= 0x7F;
        
        /* Send CMD55 first */
        BYTE buf[6];
        buf[0] = CMD55 | 0x40;
        buf[1] = 0; buf[2] = 0; buf[3] = 0; buf[4] = 0;
        buf[5] = get_crc7(buf, 5);
        
        xmit_spi_multi(buf, 6);
        
        /* Receive CMD55 response */
        n = 10;
        do {
            res = xchg_spi(0xFF);
        } while ((res & 0x80) && --n);
        
        if (res > 1) { // If CMD55 fails (not Idle or Ready)
            char cmd55_err[32];
            sprintf(cmd55_err, "[DEBUG] CMD55 Failed: %02X\r\n", res);
            UART_Print(cmd55_err);
            deselect();
            return res;
        }
        
        /* Note: CS is kept LOW here for the next command (ACMD) */
        /* Some cards require CS to stay low between CMD55 and ACMD */
        /* We consume one extra dummy byte just to be safe between commands? No, standard says immediate. */
    }

    /* Send command packet */
    BYTE buf[6];
    buf[0] = cmd | 0x40;
    buf[1] = (BYTE)(arg >> 24);
    buf[2] = (BYTE)(arg >> 16);
    buf[3] = (BYTE)(arg >> 8);
    buf[4] = (BYTE)(arg);
    buf[5] = get_crc7(buf, 5);
    if (cmd == CMD0) buf[5] = 0x95;  /* Force correct CRC for CMD0 */
    if (cmd == CMD8) buf[5] = 0x87;  /* Force correct CRC for CMD8 */
    
    xmit_spi_multi(buf, 6);

    /* Receive command response */
    if (cmd == CMD12) xchg_spi(0xFF);   /* Skip a stuff byte when stop reading */
    n = 200;                             /* Wait for a valid response (increased from 10) */
    do
        res = xchg_spi(0xFF);
    while ((res & 0x80) && --n);

    return res;         /* Return with the response value */
}


/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS SD_disk_initialize (void)
{
    BYTE n, cmd, ty, ocr[4];
    
    // DEBUG: Entry
    UART_Print("[DEBUG] SD_disk_initialize Entry\r\n");

    if (Stat & STA_NODISK) return Stat; /* No card in the socket */

    /* Low speed SPI for initialization */
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    HAL_SPI_Init(&hspi2);

    deselect();
    HAL_Delay(20);

    for (n = 10; n; n--) xchg_spi(0xFF);    /* 80 dummy clocks */

    ty = 0;
    UART_Print("[DEBUG] Sending CMD0...\r\n");
    BYTE cmd0_res = send_cmd(CMD0, 0);
    if (cmd0_res == 1) {           /* Enter Idle state */
        UART_Print("[DEBUG] CMD0 Accepted. Sending CMD8...\r\n");
        if (send_cmd(CMD8, 0x1AA) == 1) {   /* SDv2? */
            UART_Print("[DEBUG] CMD8 Accepted. Card is SDv2. Checking voltage...\r\n");
            for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);    /* Get trailing return value of R7 resp */
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {             /* The card can work at vdd range of 2.7-3.6V */
                UART_Print("[DEBUG] Voltage OK. Sending ACMD41...\r\n");
                
                // Wait for leaving idle state (ACMD41 with HCS bit)
                UINT tmr = 5000; // 5 second timeout (increased for 64GB cards)
                BYTE res_acmd41;
                
                // Try to force CRC OFF just in case
                send_cmd(59, 0); 

                do {
                    // Try with HCS bit + Voltage Window (2.7-3.6V)
                    // NOTE: Some cards are VERY picky about the gap between CMD55 and ACMD41
                    // send_cmd handles CMD55 internally if bit 0x80 is set.
                    
                    // Standard capacity (HCS=0) or High Capacity (HCS=1)?
                    // We are SDv2, so we should try HCS=1 first.
                    
                    res_acmd41 = send_cmd(ACMD41, (1UL << 30) | 0x00FF8000); // HCS | VCC
                    
                    if(res_acmd41 == 0) break;
                    
                    // If failed, try with just HCS (standard)
                    if(res_acmd41) {
                         res_acmd41 = send_cmd(ACMD41, 1UL << 30);
                    }
                    
                    HAL_Delay(10); // Increase delay to let card process
                } while (--tmr && res_acmd41);
                
                if (tmr && res_acmd41 == 0) {
                    UART_Print("[DEBUG] ACMD41 OK. Sending CMD58...\r\n");
                    if (send_cmd(CMD58, 0) == 0) {          /* Check CCS bit in the OCR */
                        UART_Print("[DEBUG] CMD58 OK. Reading OCR...\r\n");
                        for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
                        ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;  /* SDv2 */
                        UART_Print((ty & CT_BLOCK) ? "[DEBUG] Card is SDHC/SDXC (Block Addressing)\r\n" : "[DEBUG] Card is SDSC (Byte Addressing)\r\n");
                    } else {
                        UART_Print("[DEBUG] CMD58 Failed\r\n");
                    }
                } else {
                    char timeout_msg[64];
                    sprintf(timeout_msg, "[DEBUG] ACMD41 Timeout. Last Res: %02X\r\n", res_acmd41);
                    UART_Print(timeout_msg);
                }
            } else {
                UART_Print("[DEBUG] Voltage Check Failed (OCR Mismatch)\r\n");
            }
        } else {                            /* SDv1 or MMCv3 */
            UART_Print("[DEBUG] CMD8 Rejected. Card is SDv1 or MMC.\r\n");
            if (send_cmd(ACMD41, 0) <= 1)   {
                ty = CT_SD1; cmd = ACMD41;  /* SDv1 */
            } else {
                ty = CT_MMC; cmd = CMD1;    /* MMCv3 */
            }
            
            UINT tmr = 1000;
            while (--tmr && send_cmd(cmd, 0)) {      /* Wait for leaving idle state */
                HAL_Delay(1);
            }
            
            if (!tmr || send_cmd(CMD16, 512) != 0)  /* Set R/W block length to 512 */
                ty = 0;
        }
    } else {
         char msg[64];
         sprintf(msg, "[DEBUG] CMD0 Failed. Result: %02X\r\n", cmd0_res);
         UART_Print(msg);
    }
    CardType = ty;
    deselect();

    if (ty) {           /* Initialization succeeded */
        Stat &= ~STA_NOINIT;        /* Clear STA_NOINIT */
        /* High speed SPI */
        hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; // Safer speed (approx 5.6 MHz)
        HAL_SPI_Init(&hspi2);
        char msg[64];
        sprintf(msg, "[SD] Init Success. Type: %d\r\n", ty);
        UART_Print(msg);
    } else {            /* Initialization failed */
        // Stat |= STA_NOINIT;
        UART_Print("[SD] Init Failed. Type is 0.\r\n");
    }

    return Stat;
}


/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS SD_disk_status (void)
{
    return Stat;
}


/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT SD_disk_read (
    BYTE *buff,     /* Pointer to the data buffer to store read data */
    DWORD sector,   /* Start sector number (LBA) */
    UINT count      /* Sector count (1..128) */
)
{
    if (!count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;

    if (!(CardType & CT_BLOCK)) sector *= 512;  /* Convert to byte address if needed */

    if (count == 1) {   /* Single block read */
        if (send_cmd(CMD17, sector) == 0) {
             // Wait for data packet
             BYTE token;
             UINT tmr = 200;
             do {
                 token = xchg_spi(0xFF);
                 HAL_Delay(1);
             } while ((token == 0xFF) && --tmr);
             
             if(token == 0xFE) {
                rcvr_spi_multi(buff, 512);
                xchg_spi(0xFF); xchg_spi(0xFF); // Discard CRC
                count = 0;
             }
             deselect();
        }
    }
    else {              /* Multiple block read */
        if (send_cmd(CMD18, sector) == 0) {
            do {
                if (!select()) break;
                 // Wait for data packet
                 BYTE token;
                 UINT tmr = 200;
                 do {
                     token = xchg_spi(0xFF);
                     HAL_Delay(1);
                 } while ((token == 0xFF) && --tmr);

                 if(token == 0xFE) {
                    rcvr_spi_multi(buff, 512);
                    xchg_spi(0xFF); xchg_spi(0xFF);
                    buff += 512;
                 } else {
                     break; // Error
                 }
            } while (--count);
            send_cmd(CMD12, 0);             /* STOP_TRANSMISSION */
        }
    }
    deselect();

    return count ? RES_ERROR : RES_OK;
}


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT SD_disk_write (
    const BYTE *buff,   /* Pointer to the data to be written */
    DWORD sector,       /* Start sector number (LBA) */
    UINT count          /* Sector count (1..128) */
)
{
    if (!count) return RES_PARERR;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (Stat & STA_PROTECT) return RES_WRPRT;

    if (!(CardType & CT_BLOCK)) sector *= 512;  /* Convert to byte address if needed */

    if (count == 1) {   /* Single block write */
        if (send_cmd(CMD24, sector) == 0) {
            xchg_spi(0xFE); // Start token
            xmit_spi_multi(buff, 512);
            xchg_spi(0xFF); xchg_spi(0xFF); // Dummy CRC
            BYTE resp = xchg_spi(0xFF);
            if ((resp & 0x1F) == 0x05) { // Data accepted
                 while (xchg_spi(0xFF) == 0); // Wait for busy
            } else {
                count = 1; // Error
            }
            deselect();
            count = 0;
        }
    }
    else {              /* Multiple block write */
        if (CardType & CT_SDC) send_cmd(ACMD23, count);
        if (send_cmd(CMD25, sector) == 0) {
            do {
                if (!select()) break;
                xchg_spi(0xFC); // Multi-block Start Token
                xmit_spi_multi(buff, 512);
                xchg_spi(0xFF); xchg_spi(0xFF);
                BYTE resp = xchg_spi(0xFF);
                if ((resp & 0x1F) != 0x05) break;
                 while (xchg_spi(0xFF) == 0); // Wait for busy
                buff += 512;
            } while (--count);
            if (!count) {
                 if(!select()) return RES_ERROR;
                 xchg_spi(0xFD); // Stop Token
                 while (xchg_spi(0xFF) == 0); // Wait for busy
            }
        }
    }
    deselect();

    return count ? RES_ERROR : RES_OK;
}


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT SD_disk_ioctl (
    BYTE cmd,       /* Control code */
    void *buff      /* Buffer to send/receive control data */
)
{
    DRESULT res;

    if (Stat & STA_NOINIT) return RES_NOTRDY;

    res = RES_ERROR;
    switch (cmd) {
    case CTRL_SYNC :        /* Make sure that no pending write process */
        if (select()) {
             while (xchg_spi(0xFF) == 0); // Wait for busy
            res = RES_OK;
        }
        break;

    case GET_SECTOR_COUNT : /* Get number of sectors on the disk (DWORD) */
        if (send_cmd(CMD9, 0) == 0) {
            BYTE csd[16];
            BYTE token;
            UINT tmr = 200;
            
            // Wait for data packet
            do {
                token = xchg_spi(0xFF);
                HAL_Delay(1);
            } while ((token == 0xFF) && --tmr);

            if(token == 0xFE) { // Start Token
                rcvr_spi_multi(csd, 16);
                xchg_spi(0xFF); xchg_spi(0xFF); // Discard CRC
                
                // Debug Print CSD
                /*
                char msg[64];
                sprintf(msg, "[SD] CSD: %02X %02X %02X ...\r\n", csd[0], csd[1], csd[2]);
                UART_Print(msg);
                */

                if ((csd[0] >> 6) == 1) { /* SDC ver 2.00 */
                    DWORD csize = csd[9] + ((DWORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
                    *(DWORD*)buff = csize << 10;
                    char msg[64];
                    sprintf(msg, "[SD] V2 CSize: %lu Sectors\r\n", csize << 10);
                    UART_Print(msg);
                } else { /* SDC ver 1.XX or MMC */
                    DWORD n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                    DWORD csize = (csd[8] >> 6) + ((DWORD)csd[7] << 2) + ((DWORD)(csd[6] & 3) << 10) + 1;
                    *(DWORD*)buff = csize << (n - 9);
                    char msg[64];
                    sprintf(msg, "[SD] V1 CSize: %lu Sectors\r\n", csize << (n - 9));
                    UART_Print(msg);
                }
                res = RES_OK;
            } else {
                 UART_Print("[SD] CSD Read Timeout/Error\r\n");
            }
        } else {
             UART_Print("[SD] CMD9 Failed\r\n");
        }
        break;

    case GET_BLOCK_SIZE :   /* Get erase block size in unit of sector (DWORD) */
        *(DWORD*)buff = 128;
        res = RES_OK;
        break;

    default:
        res = RES_PARERR;
    }

    return res;
}
