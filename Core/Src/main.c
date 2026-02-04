/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "adxl355.h"
#include "uart_interface.h"
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Global Flags */
volatile uint8_t sensor_triggered = 0;
volatile uint8_t uart_rx_char = 0;

/* FatFs Global Variables */
FATFS SDFatFs;  /* File system object for SD card logical drive */
FIL MyFile;     /* File object */
char SDPath[4]; /* SD card logical drive path */
FRESULT res;    /* FatFs function common result code */
uint32_t byteswritten, bytesread; /* File write/read counts */

/* Configuration Variables */
float config_threshold_g = 0.5f; // Default 0.5G
ADXL355_ODR_t config_odr = ADXL355_ODR_125HZ; // Default 125Hz
uint8_t config_fifo_samples = 96; // Default 96 samples

void Show_Menu(void);
void Handle_Configuration(void);
void Start_Acquisition_Cycle(void);
void Start_Monitor_Mode(void);
void Start_Monitor_Mode(void);
void List_Files(void);
void Download_File(void);
void Mount_SD(void);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */
    // Enable UART RX Interrupt
    if (HAL_UART_Receive_IT(&huart2, (uint8_t*)&uart_rx_char, 1) != HAL_OK) {
        Error_Handler();
    }
    
    // Check if UART is actually ready
    UART_Print("\r\n=== SYSTEM BOOT ===\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    
    // Initial Menu
    UART_Print("\r\n=== ADXL355 Data Acquisition System ===\r\n");
    if (ADXL355_Init(&hspi1) != 1) {
        UART_Print("Error: ADXL355 Initialization Failed!\r\n");
        // Don't freeze, let them try again or debug
    } else {
        UART_Print("Sensor Initialized.\r\n");
    }
    
    // Check SD Card at Boot
    Mount_SD();

  while (1)
  {
        Show_Menu();
        
        // Wait for user input (handled by ISR or simple polling if blocking)
        // Here we use simple polling of the volatile char
        while (uart_rx_char == 0) {
            HAL_Delay(100);
        }
        
        char choice = uart_rx_char;
        uart_rx_char = 0; // Reset
        
        switch(choice) {
            case 'c':
            case 'C':
                Handle_Configuration();
                break;
            case 's':
            case 'S':
                Start_Acquisition_Cycle();
                break;
            case 'm':
            case 'M':
                Start_Monitor_Mode();
                break;
            case 'l':
            case 'L':
                List_Files();
                break;
            case 'd':
            case 'D':
                Download_File();
                break;
            default:
                UART_Print("Invalid Option.\r\n");
                break;
        }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, MODEM_PWRKEY_Pin|SD_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC13 ADXL_INT1_Pin_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_13|ADXL_INT1_Pin_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : ADXK_DRDY_Pin */
  GPIO_InitStruct.Pin = ADXK_DRDY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ADXK_DRDY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ADXL_CS_Pin */
  GPIO_InitStruct.Pin = ADXL_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ADXL_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : MODEM_PWRKEY_Pin SD_CS_Pin */
  GPIO_InitStruct.Pin = MODEM_PWRKEY_Pin|SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void Mount_SD(void) {
    UART_Print("\r\n[SD] Mounting SD Card...\r\n");
    
    // --- LOW LEVEL SANITY CHECK ---
    // Read Sector 0 directly to verify SPI/SD communication before Filesystem
    BYTE buff[512];
    if (disk_initialize(0) == 0) {
        if (disk_read(0, buff, 0, 1) == RES_OK) {
            char msg[64];
            sprintf(msg, "[DEBUG] Sector 0 Read: Sig=%02X%02X, PartID=%02X\r\n", buff[510], buff[511], buff[0x1C2]);
            UART_Print(msg);
        } else {
            UART_Print("[DEBUG] Sector 0 Read Failed!\r\n");
        }
    } else {
        UART_Print("[DEBUG] Disk Init Failed (Low Level)\r\n");
    }
    // ------------------------------

    // Link the driver
    // In typical FatFs generation, we need to link driver. 
    // Since we are doing manual integration without full middleware support,
    // we assume we just call f_mount.
    // NOTE: FATFS library usually needs `FATFS_LinkDriver` if using the HAL abstraction layer middleware.
    // Here we are using direct calls.
    
    // We assume pdrv=0 is our SD card.
    if(f_mount(&SDFatFs, "", 1) == FR_OK) {
        UART_Print("[SD] Mount Successful.\r\n");
        
        // --- DEBUG INFO ---
        char dbg_msg[64];
        sprintf(dbg_msg, "[DEBUG] FS Type: %d (1=FAT12,2=FAT16,3=FAT32,4=EXFAT)\r\n", SDFatFs.fs_type);
        UART_Print(dbg_msg);
        sprintf(dbg_msg, "[DEBUG] n_fatent: %lu, csize: %d\r\n", SDFatFs.n_fatent, SDFatFs.csize);
        UART_Print(dbg_msg);
        // ------------------

        // Get Free Space
        DWORD fre_clust, tot_sect;
        FATFS *fs;
        if(f_getfree("", &fre_clust, &fs) == FR_OK) {
            tot_sect = (fs->n_fatent - 2) * fs->csize;
            uint32_t tot_mb = tot_sect / 2048;
            uint32_t fre_mb = fre_clust * fs->csize / 2048;
            
            char buf[64];
            sprintf(buf, "[SD] Total Size: %lu MB\r\n", tot_mb);
            UART_Print(buf);
            sprintf(buf, "[SD] Free Space: %lu MB\r\n", fre_mb);
            UART_Print(buf);
        } else {
             UART_Print("[SD] Get Free Space Failed.\r\n");
        }
    } else {
        UART_Print("[SD] Mount Failed (Check Format/Connections).\r\n");
    }
}

void List_Files(void) {
    DIR dir;
    FILINFO fno;
    FRESULT res;
    char msg[128];
    
    Mount_SD(); // Ensure mounted
    
    res = f_opendir(&dir, "/");
    if (res == FR_OK) {
        UART_Print("\r\n--- FILE LIST ---\r\n");
        for (;;) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;
            
            if (fno.fattrib & AM_LDIR) {
                sprintf(msg, "   <DIR>   %s\r\n", fno.fname);
            } else {
                sprintf(msg, "%10lu   %s\r\n", fno.fsize, fno.fname);
            }
            UART_Print(msg);
        }
        f_closedir(&dir);
    } else {
        UART_Print("[SD] Failed to open directory.\r\n");
    }
}

void Download_File(void) {
    char fname[16];
    char ch;
    int idx = 0;
    
    UART_Print("\r\nEnter Filename (8.3 format) to download: ");
    
    // Simple line input
    while(1) {
        while(uart_rx_char == 0) HAL_Delay(10);
        ch = uart_rx_char;
        uart_rx_char = 0;
        
        if(ch == '\r' || ch == '\n') break;
        if(idx < 12) {
            fname[idx++] = ch;
            char echo[2] = {ch, 0};
            UART_Print(echo);
        }
    }
    fname[idx] = 0;
    UART_Print("\r\n");
    
    if (f_open(&MyFile, fname, FA_READ) == FR_OK) {
        UART_Print("[SD] File Opened. Streaming...\r\n");
        UART_Print("--- BEGIN FILE ---\r\n");
        
        char buffer[128];
        UINT br;
        
        while (1) {
            res = f_read(&MyFile, buffer, sizeof(buffer)-1, &br);
            if (res != FR_OK || br == 0) break;
            buffer[br] = 0;
            UART_Print(buffer);
            HAL_Delay(5); // Throttle slightly for UART
        }
        
        f_close(&MyFile);
        UART_Print("\r\n--- END FILE ---\r\n");
    } else {
        UART_Print("[SD] Failed to open file.\r\n");
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == ADXL_INT1_Pin_Pin || GPIO_Pin == GPIO_PIN_13)
  {
    sensor_triggered = 1;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    HAL_UART_Receive_IT(&huart2, (uint8_t*)&uart_rx_char, 1);
  }
}

void Show_Menu(void) {
    char msg[100];
    UART_Print("\r\n--- MENU ---\r\n");
    sprintf(msg, "Current Config: Threshold = %.2f G, ODR = %d, Buffer = %d\r\n", config_threshold_g, config_odr, config_fifo_samples);
    UART_Print(msg);
    UART_Print("[C] Configure\r\n");
    UART_Print("[S] Start Acquisition (Arm Trigger)\r\n");
    UART_Print("[M] Monitor Mode (Live Data)\r\n");
    UART_Print("[L] List Files on SD Card\r\n");
    UART_Print("[D] Download File (Print via UART)\r\n");
    UART_Print("Select option: ");
}

void Handle_Configuration(void) {
    UART_Print("\r\n[Configuration]\r\n");
    UART_Print("1. Toggle Threshold (0.1G, 0.5G, 1.0G, 2.0G)\r\n");
    UART_Print("2. Toggle ODR (125Hz, 500Hz, 1000Hz)\r\n");
    UART_Print("3. Toggle Buffer Size (32, 64, 96)\r\n");
    UART_Print("Press 1, 2 or 3: ");
    
    // Clear buffer before waiting
    uart_rx_char = 0;
    while(uart_rx_char == 0) {
        HAL_Delay(10);
    }
    char choice = uart_rx_char;
    uart_rx_char = 0; // Reset immediately
    
    // Debug echo
    char debug_msg[32];
    sprintf(debug_msg, "\r\n[DEBUG] Key pressed: %c\r\n", choice);
    UART_Print(debug_msg);

    if (choice == '1') {
        if (config_threshold_g == 0.1f) config_threshold_g = 0.5f;
        else if (config_threshold_g == 0.5f) config_threshold_g = 1.0f;
        else if (config_threshold_g == 1.0f) config_threshold_g = 2.0f;
        else config_threshold_g = 0.1f;
        UART_Print("\r\nNew Threshold set.\r\n");
    } else if (choice == '2') {
        if (config_odr == ADXL355_ODR_125HZ) config_odr = ADXL355_ODR_500HZ;
        else if (config_odr == ADXL355_ODR_500HZ) config_odr = ADXL355_ODR_1000HZ;
        else config_odr = ADXL355_ODR_125HZ;
        UART_Print("\r\nNew ODR set.\r\n");
    } else if (choice == '3') {
        if (config_fifo_samples == 96) config_fifo_samples = 32;
        else if (config_fifo_samples == 32) config_fifo_samples = 64;
        else config_fifo_samples = 96;
        UART_Print("\r\nNew Buffer Size set.\r\n");
    } else {
        UART_Print("\r\nCancelled.\r\n");
    }
}

void Start_Acquisition_Cycle(void) {
    char msg[128];
    
    // Ensure SD is mounted before starting (in case card was inserted after boot)
    Mount_SD();
    
    UART_Print("\r\n[ARMED] Configuring FIFO and Entering Sleep Mode (STOP)...\r\n");
    UART_Print("System will wake up on vibration.\r\n");
    
    // 1. Configure ODR (Must be done before other configs)
    ADXL355_Set_ODR(config_odr);

    // NEW: Mount SD Card here to ensure readiness and show status
    Mount_SD();

    // 2. Measure Baseline (Average of 100 samples)
    UART_Print("[CALIB] Measuring Baseline (100 samples)...\r\n");
    float sum_x = 0, sum_y = 0, sum_z = 0;
    int samples = 100;
    ADXL355_Data_t data;
    
    // Ensure Measurement Mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
    
    for(int i=0; i<samples; i++) {
        ADXL355_Read_Data(&data);
        sum_x += (float)data.x / 256000.0f;
        sum_y += (float)data.y / 256000.0f;
        sum_z += (float)data.z / 256000.0f;
        HAL_Delay(5); // Small delay
    }
    
    sprintf(msg, "[CALIB] Baseline: X=%.3f Y=%.3f Z=%.3f\r\n", sum_x/samples, sum_y/samples, sum_z/samples);
    UART_Print(msg);

    // 3. Enable High-Pass Filter (HPF) for AC Coupling
    // This removes the DC gravity component so 0.5G threshold works for motion
    UART_Print("[CFG] Enabling High-Pass Filter (HPF)...\r\n");
    ADXL355_Set_HPF(1);
    HAL_Delay(50); // Allow filter to settle slightly
    
    // 4. Configure FIFO (Stream Mode) to keep history
    ADXL355_Config_FIFO(config_fifo_samples);
    
    // 5. Configure Trigger (Wake-on-Motion)
    ADXL355_Config_WakeOnMotion(config_threshold_g, 2);

    // --- DIAGNOSTIC: Read back registers to confirm configuration ---
    uint8_t reg_thresh_h = ADXL355_Read_Reg(ADXL355_ACT_THRESH_H);
    uint8_t reg_thresh_l = ADXL355_Read_Reg(ADXL355_ACT_THRESH_L);
    uint8_t reg_act_en = ADXL355_Read_Reg(ADXL355_ACT_EN);
    uint8_t reg_int_map = ADXL355_Read_Reg(ADXL355_INT_MAP);
    uint8_t reg_power = ADXL355_Read_Reg(ADXL355_POWER_CTL);
    uint8_t reg_range = ADXL355_Read_Reg(ADXL355_RANGE);
    uint8_t reg_filter = ADXL355_Read_Reg(ADXL355_FILTER);
    
    sprintf(msg, "[DIAG] Regs: TH=0x%02X%02X, EN=0x%02X, MAP=0x%02X, PWR=0x%02X, RNG=0x%02X, FLT=0x%02X\r\n", 
            reg_thresh_h, reg_thresh_l, reg_act_en, reg_int_map, reg_power, reg_range, reg_filter);
    UART_Print(msg);
    // ---------------------------------------------------------------
    
    // 6. Clear Flag
    sensor_triggered = 0;
    uart_rx_char = 0; // Clear any pending input
    
    // 7. Enter STOP Mode
    // Suspend Tick to prevent SysTick from waking MCU
    HAL_SuspendTick();
    
    // Enter STOP mode (Regulator ON for fast wake up)
    HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
    
    // ... MCU Sleeps Here until EXTI Interrupt ...
    
    // 7. Wake Up
    // Resume Tick and Re-init Clock
    HAL_ResumeTick();
    SystemClock_Config();
    
    char filename[32];
    for(int i=1; i<999; i++) { 
         sprintf(filename, "DATA_%03d.CSV", i); 
         if(f_open(&MyFile, filename, FA_READ) != FR_OK) { 
             // File doesn't exist, we can use this one 
             break; 
         } 
         f_close(&MyFile); 
     } 
     
     if(f_open(&MyFile, filename, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { 
         UART_Print("[SD] Recording to "); UART_Print(filename); UART_Print("\r\n"); 
         f_puts("Timestamp_ms,X_g,Y_g,Z_g\n", &MyFile); 
     } else { 
         UART_Print("[SD] Error creating file! Data will not be saved.\r\n"); 
     } 
     // --------------------- 
     
     if (sensor_triggered) { 
         UART_Print("\r\n[WAKE] Motion Detected! Recovering History (Back-Trace)...\r\n"); 
     } else { 
         UART_Print("\r\n[WAKE] System Woke Up (No Trigger Flag). Checking Sensor Status...\r\n"); 
         // Check Status Register 
         uint8_t status = ADXL355_Read_Status(); 
         sprintf(msg, "[DIAG] Status Reg: 0x%02X\r\n", status); 
         UART_Print(msg); 
     } 
     
     // 8. Read FIFO History (Pre-Trigger Data) 
     uint8_t fifo_count = ADXL355_Get_FIFO_Entries(); 
     sprintf(msg, "[INFO] FIFO Entries: %d samples found.\r\n", fifo_count); 
     UART_Print(msg); 
     
     ADXL355_Data_t fifo_sample; 
     for(int i=0; i<fifo_count; i++) { 
         // Read one sample from FIFO 
         ADXL355_Read_FIFO(&fifo_sample, 1); 
         
         float x_g = (float)fifo_sample.x / 256000.0f; 
         float y_g = (float)fifo_sample.y / 256000.0f; 
         float z_g = (float)fifo_sample.z / 256000.0f; 
         
         // Print History 
         sprintf(msg, "[HIST] T:-%dms X:%.3f Y:%.3f Z:%.3f\r\n", (fifo_count-i)*8, x_g, y_g, z_g); 
         UART_Print(msg); 
         
         // Save to SD 
         if(MyFile.fs) { 
             sprintf(msg, "%d,%.4f,%.4f,%.4f\n", -(fifo_count-i)*8, x_g, y_g, z_g); 
             f_puts(msg, &MyFile); 
         } 
     } 
     
     UART_Print("[INFO] History Dumped. Switching to Live Stream...\r\n"); 
     UART_Print("Press 'q' to stop manually.\r\n"); 
     
     // 9. Disable HPF and Start Live Data Transmission 
     ADXL355_Set_HPF(0); // Disable HPF to show DC Gravity 
     
     // Live Transmission Loop (Live + History) 
     uint32_t last_print_time = HAL_GetTick(); 
     uint32_t last_activity_time = HAL_GetTick(); 
     
     while (1) { 
         ADXL355_Data_t data; 
         ADXL355_Read_Data(&data); // Read current data 
         
         if (HAL_GetTick() - last_print_time >= 100) { 
             float x_g = (float)data.x / 256000.0f; 
             float y_g = (float)data.y / 256000.0f; 
             float z_g = (float)data.z / 256000.0f; 
             
             sprintf(msg, "\r[ACQ] T:%lu X:%.3f G Y:%.3f G Z:%.3f G        ", data.timestamp, x_g, y_g, z_g); 
             UART_Print(msg); 
             last_print_time = HAL_GetTick(); 
         } 
         
         // Save to SD (Every sample) 
         if(MyFile.fs) { 
              float x_g = (float)data.x / 256000.0f; 
              float y_g = (float)data.y / 256000.0f; 
              float z_g = (float)data.z / 256000.0f; 
              char sd_msg[64]; 
              sprintf(sd_msg, "%lu,%.4f,%.4f,%.4f\n", HAL_GetTick(), x_g, y_g, z_g); 
              f_puts(sd_msg, &MyFile); 
              
              // Sync periodically (e.g., every 100 samples) to avoid data loss on power cut 
              static int sync_ctr = 0; 
              if(++sync_ctr >= 100) { 
                  f_sync(&MyFile); 
                  sync_ctr = 0; 
              } 
         } 
         
         // Check Manual Stop 
         if (uart_rx_char == 'q' || uart_rx_char == 'Q') { 
             UART_Print("\r\n[STOP] User Aborted.\r\n"); 
             uart_rx_char = 0; 
             break; 
         } 
         
         // Timeout check (optional) 
         // Check Activity (Simple difference check) 
         static int32_t prev_x = 0; 
         if (abs(data.x - prev_x) > 500) { 
             last_activity_time = HAL_GetTick(); 
         } 
         prev_x = data.x; 
         
         if ((HAL_GetTick() - last_activity_time) > 90000) { // 90s 
              UART_Print("\r\n[TIMEOUT] No activity for 90s. Stopping.\r\n"); 
              break; 
         } 
         
         HAL_Delay(1); 
     } 
     UART_Print("[INFO] Returning to Menu.\r\n"); 
     
     // Close SD File 
     if(MyFile.fs) { 
         f_close(&MyFile); 
         UART_Print("[SD] File Closed safely.\r\n"); 
     } 
}




void Start_Monitor_Mode(void) {
    char msg[128];
    UART_Print("\r\n[MONITOR] Starting Live Data Stream...\r\n");
    UART_Print("Press 'q' to return to menu.\r\n");
    
    // Ensure HPF is disabled
    ADXL355_Set_HPF(0);
    
    // Ensure sensor is in measurement mode
    ADXL355_Write_Reg(ADXL355_POWER_CTL, 0x00);
    
    uint32_t last_print_time = HAL_GetTick();
    uart_rx_char = 0;
    
    while (1) {
        // Check for exit command
        if (uart_rx_char == 'q' || uart_rx_char == 'Q') {
            UART_Print("\r\n[MONITOR] Stopped.\r\n");
            uart_rx_char = 0;
            break;
        }
        
        // Read and print every 100ms
        if (HAL_GetTick() - last_print_time >= 100) {
            ADXL355_Data_t data;
            ADXL355_Read_Data(&data);
            
            float x_g = (float)data.x / 256000.0f;
            float y_g = (float)data.y / 256000.0f;
            float z_g = (float)data.z / 256000.0f;
            
            // Use \r to overwrite line, padded with spaces
            sprintf(msg, "\r[MON] X:%.3f Y:%.3f Z:%.3f      ", x_g, y_g, z_g);
            UART_Print(msg);
            
            last_print_time = HAL_GetTick();
        }
    }
}

DWORD get_fattime(void) {
    // Return a fixed timestamp: 2025-01-01 12:00:00
    return ((DWORD)(2025 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16) | ((DWORD)12 << 11) | ((DWORD)0 << 5) | ((DWORD)0 >> 1);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
