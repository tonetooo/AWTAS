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
#include "Sd_spi.h"
#include "adxl355.h"
#include "ff.h"
#include "quectel_drive.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
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

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
FATFS fs;
FIL fil;
FRESULT fres;
// Shadow variables for display
const char* range_str[] = {"+/- 2g", "+/- 4g", "+/- 8g"};
const char* odr_str[] = {"?", "4000Hz", "2000Hz", "1000Hz", "500Hz", "250Hz", "125Hz", "62.5Hz", "31.25Hz"};
int cur_range_idx = 0; // Default 2g
int cur_odr_idx = 6;   // Default 125Hz
float trigger_g = 0.5f; // Default 0.5g

// Placeholder variables for future power measurement peripheral
float voltage_val = 0.0f;
float current_val = 0.0f;
float power_val = 0.0f;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI2_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void Print_Menu(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
typedef enum {
    PWR_STATE_IDLE,
    PWR_STATE_LOW_POWER_WAITING,
    PWR_STATE_ACTIVE_RECORDING
} PowerState_t;

void Print_Power_State(PowerState_t state) {
    printf("\r\n[POWER STATE: ");
    switch(state) {
        case PWR_STATE_IDLE:               printf("IDLE (Main Menu)"); break;
        case PWR_STATE_LOW_POWER_WAITING:  printf("LOW POWER (Sensor Armed - STM32 Sleep)"); break;
        case PWR_STATE_ACTIVE_RECORDING:   printf("ACTIVE (Acquisition & SD Write)"); break;
    }
    printf("]\r\n");
}

void Print_Menu(void) {
    Print_Power_State(PWR_STATE_IDLE);
    printf("\r\n--- ADXL355 Control Menu ---\r\n");
    printf("[SENSOR] Current Settings: Range: %s, ODR: %s\r\n", range_str[cur_range_idx], odr_str[cur_odr_idx]);
    printf("[SENSOR] m: Monitor Data (G)\r\n");
    printf("[SENSOR] l: Log Data to CSV\r\n");
    printf("[SENSOR] r: Set Range\r\n");
    printf("[SENSOR] o: Set ODR\r\n");
    printf("[SENSOR] t: Set Trigger (Current: %.2f G)\r\n", trigger_g);
    printf("[SENSOR] i: Interrupt Mode (Wake-on-Motion)\r\n");
    printf("[SENSOR] q: Stop/Back\r\n");
}

static void Apply_Remote_Config(const char* cfg) {
    const char* line = cfg;
    while (line && *line) {
        const char* line_end = line;
        while (*line_end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }
        size_t len = (size_t)(line_end - line);
        while (len > 0 && (line[0] == ' ' || line[0] == '\t')) {
            line++;
            len--;
        }
        if (len > 0) {
            if (len >= 6 && strncmp(line, "RANGE=", 6) == 0) {
                const char* val = line + 6;
                while (*val == ' ' || *val == '\t') {
                    val++;
                }
                char tmp[8];
                size_t vlen = 0;
                while (val[vlen] != 0 && val[vlen] != '\r' && val[vlen] != '\n' && vlen < sizeof(tmp) - 1) {
                    tmp[vlen] = val[vlen];
                    vlen++;
                }
                tmp[vlen] = 0;
                for (size_t i = 0; i < vlen; i++) {
                    if (tmp[i] >= 'a' && tmp[i] <= 'z') {
                        tmp[i] = (char)(tmp[i] - 'a' + 'A');
                    }
                }
                int g = 0;
                if (tmp[0] == '2') {
                    g = 2;
                } else if (tmp[0] == '4') {
                    g = 4;
                } else if (tmp[0] == '8') {
                    g = 8;
                } else {
                    g = atoi(tmp);
                }
                if (g <= 2) {
                    ADXL355_Set_Range(ADXL355_RANGE_2G);
                    cur_range_idx = 0;
                } else if (g <= 4) {
                    ADXL355_Set_Range(ADXL355_RANGE_4G);
                    cur_range_idx = 1;
                } else {
                    ADXL355_Set_Range(ADXL355_RANGE_8G);
                    cur_range_idx = 2;
                }
                printf("[CONFIG] RANGE set to %s\r\n", range_str[cur_range_idx]);
            } else if (len >= 7 && strncmp(line, "ODR_HZ=", 7) == 0) {
                const char* val = line + 7;
                while (*val == ' ' || *val == '\t') {
                    val++;
                }
                int odr = atoi(val);
                switch (odr) {
                    case 4000:
                        ADXL355_Set_ODR(ADXL355_ODR_4000HZ);
                        cur_odr_idx = 1;
                        break;
                    case 2000:
                        ADXL355_Set_ODR(ADXL355_ODR_2000HZ);
                        cur_odr_idx = 2;
                        break;
                    case 1000:
                        ADXL355_Set_ODR(ADXL355_ODR_1000HZ);
                        cur_odr_idx = 3;
                        break;
                    case 500:
                        ADXL355_Set_ODR(ADXL355_ODR_500HZ);
                        cur_odr_idx = 4;
                        break;
                    case 250:
                        ADXL355_Set_ODR(ADXL355_ODR_250HZ);
                        cur_odr_idx = 5;
                        break;
                    case 125:
                        ADXL355_Set_ODR(ADXL355_ODR_125HZ);
                        cur_odr_idx = 6;
                        break;
                    case 62:
                        ADXL355_Set_ODR(ADXL355_ODR_62_5HZ);
                        cur_odr_idx = 7;
                        break;
                    case 31:
                        ADXL355_Set_ODR(ADXL355_ODR_31_25HZ);
                        cur_odr_idx = 8;
                        break;
                    default:
                        break;
                }
                if (cur_odr_idx > 0) {
                    printf("[CONFIG] ODR set to %s\r\n", odr_str[cur_odr_idx]);
                }
            } else if (len >= 10 && strncmp(line, "TRIGGER_G=", 10) == 0) {
                const char* val = line + 10;
                while (*val == ' ' || *val == '\t') {
                    val++;
                }
                float t = atof(val);
                if (t > 0.0f) {
                    trigger_g = t;
                    printf("[CONFIG] Trigger set to %.2f G\r\n", trigger_g);
                }
            }
        }
        if (*line_end == 0) {
            break;
        }
        if (*line_end == '\r' && line_end[1] == '\n') {
            line_end++;
        }
        line = line_end + 1;
    }
}
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
  MX_SPI2_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  Modem_Init(&huart1);
  printf("\r\n--- AWTAS INITIALIZING (AUTONOMOUS WIRELESS TRIAXIAL ADQUISITION SYSTEM) ---\r\n");
  Modem_PowerOn();
  if (ADXL355_Init(&hspi2)) {
      printf("[SENSOR] ADXL355 Initialized Successfully\r\n");
  } else {
      printf("[SENSOR] ADXL355 Initialization Failed\r\n");
  }

  // Mount SD Card
  if (sd_mount() == 0) {
      fres = FR_OK; // Set for later checks
  } else {
      fres = FR_NOT_READY;
  }
  char config_buffer[MODEM_BUFFER_SIZE];
  if (Modem_DownloadConfig(config_buffer, sizeof(config_buffer)) == HAL_OK) {
      printf("[CONFIG] Remote configuration downloaded.\r\n");
      Apply_Remote_Config(config_buffer);
  } else {
      printf("[CONFIG] Remote configuration not applied.\r\n");
  }
  
  /* 
  // Old Manual Mount
  fres = f_mount(&fs, "", 1);
  if (fres == FR_OK) {
      printf("SD Card Mounted Successfully\r\n");
  } else {
      printf("SD Card Mount Failed: %d\r\n", fres);
  }
  */
  
  Print_Menu();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  ADXL355_Data_t data;
  uint8_t rx_byte = 0;
  uint8_t monitoring = 0;
  uint8_t logging = 0;
  char filename[20];
  char buffer[256];
  unsigned int bytes_written;
  uint32_t start_tick;
  uint32_t read_index = 0;
  
  while (1)
  {
      // Check UART input (Non-blocking)
      if (HAL_UART_Receive(&huart2, &rx_byte, 1, 0) == HAL_OK) {
          switch(rx_byte) {
              case 'm':
                  monitoring = 1;
                  printf("[SENSOR] Monitoring Started (Press 'q' to stop)\r\n");
                  break;
              case 'l':
                  if (fres != FR_OK) {
                      printf("[STORAGE] SD Card not mounted! Cannot log.\r\n");
                      break;
                  }
                  // Find next filename
                  for (int i = 1; i < 999; i++) {
                      sprintf(filename, "DATA_%03d.CSV", i);
                      if (f_open(&fil, filename, FA_READ) != FR_OK) {
                          // File does not exist, use this one
                          break;
                      }
                      f_close(&fil);
                  }
                  
                  if (f_open(&fil, filename, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
                      printf("[STORAGE] Logging to %s (Press 'q' to stop)\r\n", filename);
                      // Write Header
                    sprintf(buffer, "timestamp_rel_s;timestamp_abs;unix_time;x_g;y_g;z_g;voltaje;corriente;potencia\r\n");
                    f_write(&fil, buffer, strlen(buffer), &bytes_written);
                      logging = 1;
                      start_tick = HAL_GetTick();
                      read_index = 0;
                  } else {
                      printf("[STORAGE] Failed to open file %s\r\n", filename);
                  }
                  break;
              case 'q':
              case 'x': // Keep x for compatibility
                  if (monitoring) {
                      monitoring = 0;
                      printf("[SENSOR] Monitoring Stopped\r\n");
                  }
                  if (logging) {
                      logging = 0;
                      f_close(&fil);
                      printf("[STORAGE] Logging Stopped. File saved: %s\r\n", filename);
                      
                      // Flush UART buffer before prompting
                      while(HAL_UART_Receive(&huart2, &rx_byte, 1, 10) == HAL_OK);
                      printf("\r\nSUBIR archivo a Google Drive? (s/n): ");
                      uint8_t upload_choice = 0;
                      while(1) {
                          if (HAL_UART_Receive(&huart2, &upload_choice, 1, 100) == HAL_OK) {
                              if (upload_choice == 's' || upload_choice == 'S') {
                                  printf("Si\r\n");
                                  Modem_UploadFile(filename);
                                  break;
                              } else if (upload_choice == 'n' || upload_choice == 'N') {
                                  printf("No\r\n");
                                  break;
                              }
                          }
                      }
                  }
                  Print_Menu();
                  break;
              case 'r':
                  monitoring = 0; logging = 0;
                  printf("\r\nSelect Range (Current: %s):\r\n1: +/- 2g\r\n2: +/- 4g\r\n3: +/- 8g\r\n", range_str[cur_range_idx]);
                  HAL_UART_Receive(&huart2, &rx_byte, 1, HAL_MAX_DELAY); // Wait for input
                  if(rx_byte == '1') { ADXL355_Set_Range(ADXL355_RANGE_2G); cur_range_idx = 0; printf("Range set to 2g\r\n"); }
                  else if(rx_byte == '2') { ADXL355_Set_Range(ADXL355_RANGE_4G); cur_range_idx = 1; printf("Range set to 4g\r\n"); }
                  else if(rx_byte == '3') { ADXL355_Set_Range(ADXL355_RANGE_8G); cur_range_idx = 2; printf("Range set to 8g\r\n"); }
                  else printf("Invalid Range\r\n");
                  Print_Menu();
                  break;
              case 'o':
                  monitoring = 0; logging = 0;
                  printf("\r\nSelect ODR (Current: %s):\r\n1: 4000Hz\r\n2: 2000Hz\r\n3: 1000Hz\r\n4: 500Hz\r\n5: 250Hz\r\n6: 125Hz\r\n7: 62.5Hz\r\n8: 31.25Hz\r\n", odr_str[cur_odr_idx]);
                  HAL_UART_Receive(&huart2, &rx_byte, 1, HAL_MAX_DELAY); // Wait for input
                  switch(rx_byte) {
                      case '1': ADXL355_Set_ODR(ADXL355_ODR_4000HZ); cur_odr_idx = 1; printf("ODR set to 4000Hz\r\n"); break;
                      case '2': ADXL355_Set_ODR(ADXL355_ODR_2000HZ); cur_odr_idx = 2; printf("ODR set to 2000Hz\r\n"); break;
                      case '3': ADXL355_Set_ODR(ADXL355_ODR_1000HZ); cur_odr_idx = 3; printf("ODR set to 1000Hz\r\n"); break;
                      case '4': ADXL355_Set_ODR(ADXL355_ODR_500HZ);  cur_odr_idx = 4; printf("ODR set to 500Hz\r\n");  break;
                      case '5': ADXL355_Set_ODR(ADXL355_ODR_250HZ);  cur_odr_idx = 5; printf("ODR set to 250Hz\r\n");  break;
                      case '6': ADXL355_Set_ODR(ADXL355_ODR_125HZ);  cur_odr_idx = 6; printf("ODR set to 125Hz\r\n");  break;
                      case '7': ADXL355_Set_ODR(ADXL355_ODR_62_5HZ); cur_odr_idx = 7; printf("ODR set to 62.5Hz\r\n"); break;
                      case '8': ADXL355_Set_ODR(ADXL355_ODR_31_25HZ); cur_odr_idx = 8; printf("ODR set to 31.25Hz\r\n"); break;
                      default: printf("Invalid ODR\r\n"); break;
                  }
                  Print_Menu();
                  break;
              case 't': {
                  monitoring = 0; logging = 0;
                  printf("\r\n--- Set Trigger Threshold ---\r\n");
                  printf("Current: %.2f G\r\n", trigger_g);
                  printf("Enter new value [e.g. 0.5] (q to cancel, Enter to save): ");
                  
                  char t_buf[10];
                  int t_idx = 0;
                  uint8_t exit_case = 0;
                  
                  // Flush UART buffer to remove any leftover characters (like \r or \n)
                  while(HAL_UART_Receive(&huart2, &rx_byte, 1, 10) == HAL_OK);
                  
                  while(1) {
                      if(HAL_UART_Receive(&huart2, &rx_byte, 1, 100) == HAL_OK) {
                          if(rx_byte == 'q' || rx_byte == 'Q') {
                              printf("\r\nCancelled.\r\n");
                              exit_case = 1;
                              break;
                          }
                          if(rx_byte == '\r' || rx_byte == '\n') {
                              if (t_idx > 0) break; // Save if something was typed
                              else { // Just Enter without value
                                  printf("\r\nCancelled (No value entered).\r\n");
                                  exit_case = 1;
                                  break;
                              }
                          }
                          // Handle Backspace (ASCII 8 or 127)
                          if(rx_byte == 8 || rx_byte == 127) {
                              if(t_idx > 0) {
                                  t_idx--;
                                  printf("\b \b"); // Move back, print space, move back again
                              }
                              continue;
                          }
                          // Only allow numbers and decimal point
                          if((rx_byte >= '0' && rx_byte <= '9') || rx_byte == '.') {
                              if(t_idx < 9) {
                                  t_buf[t_idx++] = rx_byte;
                                  HAL_UART_Transmit(&huart2, &rx_byte, 1, 100); // Echo
                              }
                          } else {
                              // Ignore other characters without printing error to keep it clean
                          }
                      }
                  }
                  
                  if (!exit_case && t_idx > 0) {
                      t_buf[t_idx] = 0;
                      float new_trigger = atof(t_buf);
                      if (new_trigger > 0.0f) {
                          trigger_g = new_trigger;
                          printf("\r\nSUCCESS: Trigger set to %.2f G\r\n", trigger_g);
                      } else {
                          printf("\r\nERROR: Invalid numeric value.\r\n");
                      }
                  }
                  
                  // Final menu display to ensure we know where we are
                  Print_Menu();
                  break;
              }

              case 'i': {
                  monitoring = 0; logging = 0;
                  if (fres != FR_OK) { printf("SD Not Mounted!\r\n"); break; }
                  printf("\r\nARMING INTERRUPT MODE (Threshold: %.2f G)...\r\n", trigger_g);
                  ADXL355_Config_WakeOnMotion(trigger_g, 5); // Count=5
                  printf("ARMED. Waiting for motion... (Press 'q' to abort)\r\n");
                  Print_Power_State(PWR_STATE_LOW_POWER_WAITING);
                  
                  uint8_t waiting = 1;
                  uint32_t last_display_tick = 0;
                  while(waiting) {
                      // Show current values while waiting for motion
                      if (HAL_GetTick() - last_display_tick > 150) {
                          ADXL355_Read_Data(&data);
                          int32_t x_mg = (int32_t)(data.x_g * 1000);
                          int32_t y_mg = (int32_t)(data.y_g * 1000);
                          int32_t z_mg = (int32_t)(data.z_g * 1000);
                          printf("\rWait... X: %ld.%03ld | Y: %ld.%03ld | Z: %ld.%03ld g  ", 
                                 x_mg/1000, (x_mg<0?-x_mg:x_mg)%1000, 
                                 y_mg/1000, (y_mg<0?-y_mg:y_mg)%1000, 
                                 z_mg/1000, (z_mg<0?-z_mg:z_mg)%1000);
                          last_display_tick = HAL_GetTick();
                      }

                      // Check INT1 (Active High)
                      if(HAL_GPIO_ReadPin(ADXL_INT1_GPIO_Port, ADXL_INT1_Pin) == GPIO_PIN_SET) {
                          Print_Power_State(PWR_STATE_ACTIVE_RECORDING);
                          printf("MOTION DETECTED! Recording...\r\n");
                          
                          // Find filename for trigger log
                          for (int i = 1; i < 999; i++) {
                              sprintf(filename, "TRIG_%03d.CSV", i);
                              if (f_open(&fil, filename, FA_READ) != FR_OK) break;
                              f_close(&fil);
                          }
                          
                          if (f_open(&fil, filename, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
                                printf("Logging to %s\r\n", filename);
                                // Header with semi-colon
                                sprintf(buffer, "timestamp_rel_s;timestamp_abs;unix_time;x_g;y_g;z_g;voltaje;corriente;potencia\r\n");
                                f_write(&fil, buffer, strlen(buffer), &bytes_written);
                              
                              start_tick = HAL_GetTick();
                              read_index = 0;
                              uint32_t rec_start = HAL_GetTick();
                              uint8_t earthquake_detected = 0;
                              
                              // Settling logic variables
                              uint32_t settling_start = 0;
                              uint8_t in_settling = 0;
                              float prev_mag = -1.0f;
                              const float static_delta = 0.005f; // 5mg stability threshold
                              const uint32_t settling_duration = 3000; // 3 seconds of stability required
                              
                              // Record for up to 60 seconds or until settled/aborted
                              while((HAL_GetTick() - rec_start) < 60000) {
                                  ADXL355_Read_Data(&data);
                                  
                                  // 1. Calculate Magnitude (XY only, Z is ignored due to 1G gravity)
                                  float current_mag = sqrtf(data.x_g * data.x_g + data.y_g * data.y_g);
                                  
                                  // 2. Auto-stop logic: check if event has finished
                                  if (current_mag < trigger_g) {
                                      if (!in_settling) {
                                          in_settling = 1;
                                          settling_start = HAL_GetTick();
                                          printf("\r\nSETTLING: Starting settling counter (3000 ms)...\r\n");
                                      }
                                      
                                      // Check if "static" (change between samples is very low)
                                      float delta = (prev_mag < 0) ? 0 : fabsf(current_mag - prev_mag);
                                      if (delta > static_delta) {
                                          // Still moving significantly, reset settling timer
                                          if ((HAL_GetTick() - settling_start) > 500) { // Avoid spamming if very close to delta
                                              printf("Moving... resetting settling time.\r\n");
                                          }
                                          settling_start = HAL_GetTick();
                                      }
                                      
                                      // If stable for the required duration, stop recording
                                      if ((HAL_GetTick() - settling_start) > settling_duration) {
                                          printf("\r\nEVENT FINISHED: System settled.\r\n");
                                          break;
                                      }
                                  } else {
                                      // Back above threshold, event is still active
                                      if (in_settling) {
                                          printf("\r\nNEW EVENT DETECTED: Interrupting settling, continuing log...\r\n");
                                      }
                                      in_settling = 0;
                                  }
                                  prev_mag = current_mag;
                                  
                                  // Earthquake Rejection Logic (Abort if > 2.0g)
                                  if ((data.x_g > 2.0f || data.x_g < -2.0f) || 
                                      (data.y_g > 2.0f || data.y_g < -2.0f) || 
                                      (data.z_g > 2.0f || data.z_g < -2.0f)) {
                                      printf("\r\nEARTHQUAKE/SHOCK DETECTED (>2.0G)! Aborting...\r\n");
                                      earthquake_detected = 1;
                                      break;
                                  }
                                  
                                  uint8_t fifo_entries = ADXL355_Get_FIFO_Entries();
                                  uint32_t current_tick = HAL_GetTick();
                                  uint32_t elapsed_ms = current_tick - start_tick;
                                  
                                  uint32_t base_sec = 1767817653; 
                                  uint32_t rel_sec = elapsed_ms / 1000;
                                  uint32_t rel_us = (elapsed_ms % 1000) * 1000;
                                  
                                  int32_t x_ug = (int32_t)(data.x_g * 1000000);
                                  int32_t y_ug = (int32_t)(data.y_g * 1000000);
                                  int32_t z_ug = (int32_t)(data.z_g * 1000000);
                    
                                  sprintf(buffer, "%lu.%06lu;%lu.%06lu;%lu.%06lu;%ld.%06ld;%ld.%06ld;%ld.%06ld;%.2f;%.2f;%.2f\r\n", 
                                         rel_sec, rel_us, base_sec + rel_sec, rel_us, base_sec + rel_sec, rel_us,
                                         x_ug/1000000, (x_ug<0?-x_ug:x_ug)%1000000,
                                         y_ug/1000000, (y_ug<0?-y_ug:y_ug)%1000000,
                                         z_ug/1000000, (z_ug<0?-z_ug:z_ug)%1000000,
                                         voltage_val, current_val, power_val);
                                  
                                  f_write(&fil, buffer, strlen(buffer), &bytes_written);
                                  read_index++; // Increment read_index for internal tracking
                                  
                                  if(HAL_UART_Receive(&huart2, &rx_byte, 1, 0) == HAL_OK && rx_byte == 'q') break;
                              }
                              f_close(&fil);
                              
                              if(earthquake_detected) {
                                  // Optional: Delete the file if earthquake
                                  // f_unlink(filename); 
                                  printf("Log file kept but marked as potentially invalid (Shock).\r\n");
                              } else {
                                  printf("Recording Finished.\r\n");
                                  
                                  // Preguntar por subida a Drive
                                  printf("\r\nSUBIR archivo a Google Drive? (s/n): ");
                                  uint8_t upload_choice_i = 0;
                                  while(1) {
                                      if (HAL_UART_Receive(&huart2, &upload_choice_i, 1, 100) == HAL_OK) {
                                          if (upload_choice_i == 's' || upload_choice_i == 'S') {
                                              printf("Si\r\n");
                                              Modem_UploadFile(filename);
                                              break;
                                          } else if (upload_choice_i == 'n' || upload_choice_i == 'N') {
                                              printf("No\r\n");
                                              break;
                                          }
                                      }
                                  }
                              }
                          } else {
                              printf("Failed to open file %s\r\n", filename);
                          }
                          waiting = 0;
                      }
                      
                      if(HAL_UART_Receive(&huart2, &rx_byte, 1, 0) == HAL_OK && (rx_byte == 'q' || rx_byte == 'Q')) {
                          printf("Aborted.\r\n");
                          waiting = 0;
                      }
                  }
                  
                  // Restore Settings
                  ADXL355_Write_Reg(ADXL355_ACT_EN, 0x00);
                  // Restore Range (Index 0 -> 2G (0x01), etc.)
                  ADXL355_Set_Range((ADXL355_Range_t)(cur_range_idx + 1));
                  // Restore ODR
                  if(cur_odr_idx > 0) ADXL355_Set_ODR((ADXL355_ODR_t)(cur_odr_idx - 1));
                  
                  printf("System Restored.\r\n");
                  Print_Menu();
                  break;
              }
          }
      }

      if (monitoring) {
          ADXL355_Read_Data(&data);
          // Print as integer * 1000 to avoid float printf issues
          int32_t x_mg = (int32_t)(data.x_g * 1000);
          int32_t y_mg = (int32_t)(data.y_g * 1000);
          int32_t z_mg = (int32_t)(data.z_g * 1000);
          
          printf("X: %ld.%03ld g | Y: %ld.%03ld g | Z: %ld.%03ld g\r\n", 
                 x_mg/1000, (x_mg<0?-x_mg:x_mg)%1000, 
                 y_mg/1000, (y_mg<0?-y_mg:y_mg)%1000, 
                 z_mg/1000, (z_mg<0?-z_mg:z_mg)%1000);
                 
          HAL_Delay(100); // 10Hz Update for display
      }

      if (logging) {
          ADXL355_Read_Data(&data);
          uint8_t fifo_entries = ADXL355_Get_FIFO_Entries();
          uint32_t current_tick = HAL_GetTick();
          uint32_t elapsed_ms = current_tick - start_tick;
          
          // Format for CSV: timestamp_rel_s,timestamp_abs,unix_time,x_g,y_g,z_g,voltaje,corriente,potencia
          // Simulate timestamps using elapsed time + dummy base (from example: 1767817653)
          uint32_t base_sec = 1767817653; 
          uint32_t rel_sec = elapsed_ms / 1000;
          uint32_t rel_us = (elapsed_ms % 1000) * 1000;
          
          // Use integer math for 6 decimal precision (micro-g)
          int32_t x_ug = (int32_t)(data.x_g * 1000000);
          int32_t y_ug = (int32_t)(data.y_g * 1000000);
          int32_t z_ug = (int32_t)(data.z_g * 1000000);

          sprintf(buffer, "%lu.%06lu;%lu.%06lu;%lu.%06lu;%ld.%06ld;%ld.%06ld;%ld.%06ld;%.2f;%.2f;%.2f\r\n", 
                 rel_sec, rel_us,                             // timestamp_rel_s
                 base_sec + rel_sec, rel_us,                  // timestamp_abs
                 base_sec + rel_sec, rel_us,                  // unix_time
                 x_ug/1000000, (x_ug<0?-x_ug:x_ug)%1000000,   // x_g
                 y_ug/1000000, (y_ug<0?-y_ug:y_ug)%1000000,   // y_g
                 z_ug/1000000, (z_ug<0?-z_ug:z_ug)%1000000,   // z_g
                 voltage_val, current_val, power_val);        // Placeholders for power peripheral
          
          f_write(&fil, buffer, strlen(buffer), &bytes_written);
          read_index++; // Increment read_index even if not logged in CSV for internal tracking
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
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
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
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  HAL_GPIO_WritePin(ADXL_CS_GPIO_Port, ADXL_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, HAT_PWR_OFF_Pin|MODEM_PWRKEY_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : ADXK_DRDY_Pin */
  GPIO_InitStruct.Pin = ADXK_DRDY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ADXK_DRDY_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ADXL_CS_Pin */
  GPIO_InitStruct.Pin = ADXL_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(ADXL_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : HAT_PWR_OFF_Pin MODEM_PWRKEY_Pin */
  GPIO_InitStruct.Pin = HAT_PWR_OFF_Pin|MODEM_PWRKEY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : MODEM_RI_Pin */
  GPIO_InitStruct.Pin = MODEM_RI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(MODEM_RI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ADXL_INT1_Pin */
  GPIO_InitStruct.Pin = ADXL_INT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(ADXL_INT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
// Redirect printf to UART
int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
  return len;
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
