/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SD_CS_Pin GPIO_PIN_13
#define SD_CS_GPIO_Port GPIOC
#define FLASH_CS_Pin GPIO_PIN_15
#define FLASH_CS_GPIO_Port GPIOC
#define FLASH_HOLD_Pin GPIO_PIN_0
#define FLASH_HOLD_GPIO_Port GPIOC
#define PYRO_3_Pin GPIO_PIN_1
#define PYRO_3_GPIO_Port GPIOC
#define PYRO_2_Pin GPIO_PIN_2
#define PYRO_2_GPIO_Port GPIOC
#define PYRO_1_Pin GPIO_PIN_3
#define PYRO_1_GPIO_Port GPIOC
#define FLASH_WP_Pin GPIO_PIN_4
#define FLASH_WP_GPIO_Port GPIOA
#define MS5611_CS_Pin GPIO_PIN_4
#define MS5611_CS_GPIO_Port GPIOC
#define KX134_CS_Pin GPIO_PIN_1
#define KX134_CS_GPIO_Port GPIOB
#define BUZZER_Pin GPIO_PIN_12
#define BUZZER_GPIO_Port GPIOB
#define GPS_RESET_Pin GPIO_PIN_8
#define GPS_RESET_GPIO_Port GPIOC
#define LED_Pin GPIO_PIN_9
#define LED_GPIO_Port GPIOA
#define GPS_IMPULSE_Pin GPIO_PIN_10
#define GPS_IMPULSE_GPIO_Port GPIOA
#define PYRO_4_Pin GPIO_PIN_9
#define PYRO_4_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
