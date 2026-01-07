/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main_rocket_fixed.c
  * @brief          : Máquina de estados del cohete con clock corregido
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "fatfs.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"

// Hardware classes
#include "KX134.h"
#include "MS5611.h"
#include "ZOE_M8Q.h"
#include "WS2812B.h"
#include "Buzzer.h"
#include "SPIFlash.h"
#include "PyroChannels.h"
#include "ServoControl.h"
#include "SDLogger.h"
#include "RocketStateMachine.h"
#include <stdio.h>

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
// Hardware instances
KX134_t kx134;
MS5611_t ms5611;
ZOE_M8Q_t gps;
WS2812B_t led;
Buzzer_t buzzer;
SPIFlash_t spiflash;
ServoControl_t servo;
SDLogger_t sdlogger;

// Rocket state machine instance
RocketStateMachine_t rocket;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    /* MCU Configuration--------------------------------------------------------*/
    HAL_Init();
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_TIM1_Init();
    MX_TIM2_Init();
    MX_TIM4_Init();
    MX_SPI1_Init();
    MX_I2C3_Init();
    MX_FATFS_Init();

    /* USER CODE BEGIN 2 */

    // Initial delay for stability
    HAL_Delay(2000);

    // Initialize LED FIRST for error indication
    WS2812B_Init(&led, &htim1, TIM_CHANNEL_2);
    WS2812B_SetColorRGB(&led, 255, 255, 255); // White = initializing

    // Initialize Buzzer for audio feedback
    Buzzer_Init(&buzzer);

    // Initialize SD card logger
    bool sd_ok = SDLogger_Init(&sdlogger);

    if (!sd_ok) {
        // SD initialization failed - LED red blink FAST
        while (1) {
            WS2812B_SetColorRGB(&led, 255, 0, 0);
            HAL_Delay(100);
            WS2812B_SetColorRGB(&led, 0, 0, 0);
            HAL_Delay(100);
        }
    }

    // Create debug log file
    if (!SDLogger_CreateDebugFile(&sdlogger)) {
        // Debug file creation failed - LED orange blink
        while (1) {
            WS2812B_SetColorRGB(&led, 255, 165, 0);
            HAL_Delay(200);
            WS2812B_SetColorRGB(&led, 0, 0, 0);
            HAL_Delay(200);
        }
    }

    // SD initialized successfully - LED green for 1 second
    WS2812B_SetColorRGB(&led, 0, 255, 0);
    HAL_Delay(1000);
	WS2812B_SetColorRGB(&led, 0, 0, 0);

    // Test write immediately
    SDLogger_WriteText(&sdlogger, "=== SYSTEM BOOT ===");
    SDLogger_WriteText(&sdlogger, "Master MCU Starting...");
    SDLogger_WriteText(&sdlogger, "SD Card initialization: SUCCESS");

    char test_msg[100];
    sprintf(test_msg, "System time: %lu ms", HAL_GetTick());
    SDLogger_WriteText(&sdlogger, test_msg);

    // Initialize pyro channels (safe by default)
    PyroChannels_Init();
    SDLogger_WriteText(&sdlogger, "Pyro channels initialized (safe mode)");

    // Initialize rocket state machine with all hardware
    if (!RocketStateMachine_Init(&rocket, &kx134, &ms5611, &gps, &led, &buzzer, &spiflash)) {
        // Initialization failed - enter error loop with red LED
        SDLogger_WriteText(&sdlogger, "ERROR: State machine initialization failed!");
        WS2812B_SetColorRGB(&led, 255, 0, 0);
        while (1) {
            HAL_Delay(500);
        }
    }

    SDLogger_WriteText(&sdlogger, "State machine initialized successfully");

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        // Update rocket state machine
        RocketStateMachine_Update(&rocket);

        // Small delay to prevent excessive CPU usage (state machine handles timing internally)
        HAL_Delay(1);

        /* USER CODE END WHILE */
        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration - CONFIGURACIÓN CORREGIDA 80MHz
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;     // ← 80MHz configuración
    RCC_OscInitStruct.PLL.PLLN = 80;    // ← 80MHz configuración
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);  // ← 80MHz configuración
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
    }
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
