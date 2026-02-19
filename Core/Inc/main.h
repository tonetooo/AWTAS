/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
extern volatile uint8_t g_event_pending;
extern volatile uint8_t g_modem_abort_enabled;

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ADXK_DRDY_Pin GPIO_PIN_0
#define ADXK_DRDY_GPIO_Port GPIOC
#define ADXL_CS_Pin GPIO_PIN_4
#define ADXL_CS_GPIO_Port GPIOA
#define HAT_PWR_OFF_Pin GPIO_PIN_0
#define HAT_PWR_OFF_GPIO_Port GPIOB
#define MODEM_PWRKEY_Pin GPIO_PIN_1
#define MODEM_PWRKEY_GPIO_Port GPIOB
#define MODEM_RI_Pin GPIO_PIN_2
#define MODEM_RI_GPIO_Port GPIOB
#define ADXL_INT1_Pin GPIO_PIN_7
#define ADXL_INT1_GPIO_Port GPIOC
#define ADXL_INT1_EXTI_IRQn EXTI9_5_IRQn
#define SD_CS_Pin GPIO_PIN_6
#define SD_CS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define SD_CS_Pin GPIO_PIN_6
#define SD_CS_GPIO_Port GPIOB
#define ADXK_DRDY_Pin GPIO_PIN_0
#define ADXK_DRDY_GPIO_Port GPIOC
#define ADXL_CS_Pin GPIO_PIN_4
#define ADXL_CS_GPIO_Port GPIOA
#define ADXL_INT1_Pin GPIO_PIN_7
#define ADXL_INT1_GPIO_Port GPIOC
#define ADXL_INT1_EXTI_IRQn EXTI9_5_IRQn

/* MODEM QUECTEL DEFINES */
#define MODEM_UART_TX_Pin GPIO_PIN_9
#define MODEM_UART_TX_GPIO_Port GPIOA
#define MODEM_UART_RX_Pin GPIO_PIN_10
#define MODEM_UART_RX_GPIO_Port GPIOA

#define HAT_PWR_OFF_Pin GPIO_PIN_0
#define HAT_PWR_OFF_GPIO_Port GPIOB
#define MODEM_PWRKEY_Pin GPIO_PIN_1
#define MODEM_PWRKEY_GPIO_Port GPIOB
#define MODEM_RI_Pin GPIO_PIN_2
#define MODEM_RI_GPIO_Port GPIOB
#define MODEM_RI_EXTI_IRQn EXTI2_IRQn
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
