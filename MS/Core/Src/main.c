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
#include "HardwareTest.h"
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

// Hardware test instance
HardwareTest_t hwtest;

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

    // Delay inicial para estabilización del hardware
    HAL_Delay(2000);

    // Initialize hardware test structure
    HardwareTest_Init(&hwtest);

    // Assign hardware instance pointers
    hwtest.hardware.kx134 = &kx134;
    hwtest.hardware.ms5611 = &ms5611;
    hwtest.hardware.gps = &gps;
    hwtest.hardware.flash = &spiflash;
    hwtest.hardware.led = &led;
    hwtest.hardware.buzzer = &buzzer;
    hwtest.hardware.sdlogger = &sdlogger;
    hwtest.hardware.servo = &servo;

    // Initialize pyro channels (safe by default)
    PyroChannels_Init();

    // Run all hardware tests sequentially
    HardwareTest_RunAll(&hwtest);

    // After tests complete, enter continuous monitoring mode
    // This allows real-time verification of sensor readings
    HardwareTest_ContinuousMonitoring(&hwtest);

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        // Infinite loop - continuous monitoring runs indefinitely

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
