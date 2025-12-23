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
#include "../../MIDWARE/RocketStateMachine.h"
#include "SDLogger.h"
#include <stdio.h>

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
#define DEBUG_UPDATE_INTERVAL_MS    1000    // Debug info every second

/* Private variables ---------------------------------------------------------*/
// Hardware instances
KX134_t kx134;
MS5611_t ms5611;
ZOE_M8Q_t gps;
WS2812B_t led;
Buzzer_t buzzer;
SPIFlash_t spiflash;
RocketStateMachine_t rocket;
SDLogger_t sdlogger;

// Debug variables
uint32_t last_debug_time = 0;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static bool InitializeAllSensors(void);
static void PrintRocketStatus(void);
static void SimulateLaunchSequence(void);

/* USER CODE BEGIN 0 */

/**
 * @brief Inicializa todos los sensores y componentes
 */
static bool InitializeAllSensors(void) {
    bool all_ok = true;

    // PRIMERO: Inicializar SD Logger para poder escribir logs
    if (!SDLogger_Init(&sdlogger)) {
        // Si no hay SD, inicializar LED para indicar problema
        WS2812B_Init(&led, &htim1, TIM_CHANNEL_2);
        WS2812B_SetColorRGB(&led, 255, 255, 0); // Amarillo = warning SD
        HAL_Delay(2000);
    } else {
    	SDLogger_CreateDebugFile(&sdlogger);
        SDLogger_WriteText(&sdlogger, "SD Logger inicializado correctamente - Logs disponibles");
    }
    SDLogger_WriteText(&sdlogger, "=== INICIO DE INICIALIZACIÓN DE SENSORES ===");

    // SEGUNDO: Inicializar SPI Flash inmediatamente para recuperación
    SDLogger_WriteText(&sdlogger, "Inicializando SPI Flash W25Q128...");
    if (!SPIFlash_Init(&spiflash, &hspi1)) {
        WS2812B_SetColorRGB(&led, 255, 0, 0); // Rojo = error
        SDLogger_WriteText(&sdlogger, "ERROR CRÍTICO: Fallo en inicialización del SPI Flash");
        SDLogger_WriteText(&sdlogger, "  - Verificar conexiones SPI1");
        SDLogger_WriteText(&sdlogger, "  - Verificar pin CS en PC15");
        SDLogger_WriteText(&sdlogger, "  - Verificar pins WP y HOLD");
        SDLogger_WriteText(&sdlogger, "  - Verificar chip W25Q128JVS");
        SDLogger_WriteText(&sdlogger, "  - SIN FLASH NO HAY LOGGING DE VUELO!");
        all_ok = false;
    } else {
        SDLogger_WriteText(&sdlogger, "SPI Flash W25Q128 inicializado correctamente");

        // TERCERO: Verificar y recuperar datos previos INMEDIATAMENTE
        SDLogger_WriteText(&sdlogger, "");
        SDLogger_WriteText(&sdlogger, "=== SISTEMA DE RECUPERACIÓN AUTOMÁTICA ===");
        RocketStateMachine_CheckAndRecoverFlashData_EarlyInit(&spiflash);
        SDLogger_WriteText(&sdlogger, "=== FIN DE RECUPERACIÓN - CONTINUANDO INICIALIZACIÓN ===");
        SDLogger_WriteText(&sdlogger, "");
    }

    // Inicializar LED y Buzzer para debug visual/auditivo
    SDLogger_WriteText(&sdlogger, "Inicializando LED WS2812B...");
    if (!WS2812B_Init(&led, &htim1, TIM_CHANNEL_2)) {
        all_ok = false;
        SDLogger_WriteText(&sdlogger, "ERROR: Fallo en inicialización del LED WS2812B");
    } else {
        WS2812B_SetColorRGB(&led, 255, 0, 255); // Magenta = inicializando
        SDLogger_WriteText(&sdlogger, "LED WS2812B inicializado correctamente");
    }

    SDLogger_WriteText(&sdlogger, "Inicializando Buzzer...");
    if (!Buzzer_Init(&buzzer)) {
        all_ok = false;
        SDLogger_WriteText(&sdlogger, "ERROR: Fallo en inicialización del Buzzer");
    } else {
        BUZZER_SUCCESS(&buzzer); // Señal de inicio
        SDLogger_WriteText(&sdlogger, "Buzzer inicializado correctamente");
    }

    // Inicializar PyroChannels
    SDLogger_WriteText(&sdlogger, "Inicializando PyroChannels...");
    PyroChannels_Init();
    SDLogger_WriteText(&sdlogger, "PyroChannels inicializados correctamente (todos desactivados)");

    HAL_Delay(500);

    // Inicializar acelerómetro KX134
    SDLogger_WriteText(&sdlogger, "Inicializando acelerómetro KX134...");
    if (!KX134_Init(&kx134, &hspi1, GPIOB, GPIO_PIN_1)) {
        WS2812B_SetColorRGB(&led, 255, 0, 0); // Rojo = error
        BUZZER_ERROR(&buzzer);
        SDLogger_WriteText(&sdlogger, "ERROR CRÍTICO: Fallo en inicialización del acelerómetro KX134");
        SDLogger_WriteText(&sdlogger, "  - Verificar conexiones SPI1");
        SDLogger_WriteText(&sdlogger, "  - Verificar pin CS en GPIOB PIN_1");
        SDLogger_WriteText(&sdlogger, "  - Verificar alimentación del sensor");
        all_ok = false;
    } else {
        WS2812B_SetColorRGB(&led, 0, 255, 0); // Verde = OK
        BUZZER_SUCCESS(&buzzer);
        SDLogger_WriteText(&sdlogger, "KX134 acelerómetro inicializado correctamente");
    }

    HAL_Delay(500);

    // Inicializar barómetro MS5611
    SDLogger_WriteText(&sdlogger, "Inicializando barómetro MS5611...");
    if (!MS5611_Init(&ms5611, &hspi1, GPIOC, GPIO_PIN_4)) {
        WS2812B_SetColorRGB(&led, 255, 0, 0); // Rojo = error
        BUZZER_ERROR(&buzzer);
        SDLogger_WriteText(&sdlogger, "ERROR CRÍTICO: Fallo en inicialización del barómetro MS5611");
        SDLogger_WriteText(&sdlogger, "  - Verificar conexiones SPI1");
        SDLogger_WriteText(&sdlogger, "  - Verificar pin CS en GPIOC PIN_4");
        SDLogger_WriteText(&sdlogger, "  - Verificar calibración PROM");
        SDLogger_WriteText(&sdlogger, "  - Verificar alimentación del sensor");
        all_ok = false;
    } else {
        WS2812B_SetColorRGB(&led, 0, 255, 0); // Verde = OK
        BUZZER_SUCCESS(&buzzer);
        SDLogger_WriteText(&sdlogger, "MS5611 barómetro inicializado correctamente");
    }

    HAL_Delay(500);

    // Inicializar GPS ZOE-M8Q
    SDLogger_WriteText(&sdlogger, "Inicializando GPS ZOE-M8Q...");
    if (!ZOE_M8Q_Init(&gps, &hi2c3)) {
        WS2812B_SetColorRGB(&led, 255, 255, 0); // Amarillo = warning (GPS puede tardar)
        Buzzer_Pattern(&buzzer, BUZZER_PATTERN_INIT);
        SDLogger_WriteText(&sdlogger, "WARNING: Fallo en inicialización del GPS ZOE-M8Q");
        SDLogger_WriteText(&sdlogger, "  - Verificar conexiones I2C3");
        SDLogger_WriteText(&sdlogger, "  - Verificar dirección I2C 0x42");
        SDLogger_WriteText(&sdlogger, "  - Verificar alimentación del GPS");
        SDLogger_WriteText(&sdlogger, "  - GPS no es crítico, continuando...");
        // GPS no es crítico para el test inicial
    } else {
        WS2812B_SetColorRGB(&led, 0, 255, 0); // Verde = OK
        BUZZER_SUCCESS(&buzzer);
        SDLogger_WriteText(&sdlogger, "ZOE-M8Q GPS inicializado correctamente");

        // Dar tiempo al GPS para obtener fix inicial
        SDLogger_WriteText(&sdlogger, "GPS: Esperando fix inicial...");
        uint32_t gps_wait_start = HAL_GetTick();
        while ((HAL_GetTick() - gps_wait_start) < 120000) { // Esperar hasta 2 minutos
            ZOE_M8Q_ReadData(&gps);
            if (ZOE_M8Q_HasValidFix(&gps)) {
                WS2812B_SetColorRGB(&led, 0, 255, 255); // Cyan = GPS fix obtenido
                BUZZER_SUCCESS(&buzzer);
                SDLogger_WriteText(&sdlogger, "GPS: Fix obtenido exitosamente");
                break;
            }
            HAL_Delay(1000); // Chequear cada segundo
            // Parpadear LED amarillo mientras espera
            WS2812B_SetColorRGB(&led, 255, 255, 0);
            HAL_Delay(100);
            WS2812B_SetColorRGB(&led, 0, 0, 0);
            HAL_Delay(100);
        }

        if (!ZOE_M8Q_HasValidFix(&gps)) {
            SDLogger_WriteText(&sdlogger, "GPS: Sin fix después de 2 minutos, continuando sin GPS");
            WS2812B_SetColorRGB(&led, 255, 255, 0); // Amarillo = warning
        }
    }

    HAL_Delay(500);

    // Resumen final de inicialización
    SDLogger_WriteText(&sdlogger, "");
    SDLogger_WriteText(&sdlogger, "=== RESUMEN DE INICIALIZACIÓN ===");
    if (all_ok) {
        SDLogger_WriteText(&sdlogger, "✓ TODOS LOS SENSORES CRÍTICOS INICIALIZADOS CORRECTAMENTE");
        SDLogger_WriteText(&sdlogger, "✓ Sistema listo para operación de vuelo");
    } else {
        SDLogger_WriteText(&sdlogger, "✗ FALLOS DETECTADOS EN SENSORES CRÍTICOS");
        SDLogger_WriteText(&sdlogger, "✗ Revisar errores arriba antes de vuelo");
        SDLogger_WriteText(&sdlogger, "✗ Sistema NO apto para vuelo seguro");
    }
    SDLogger_WriteText(&sdlogger, "");

    return all_ok;
}

/**
 * @brief Imprime el estado actual del cohete via SD logger
 */
static void PrintRocketStatus(void) {
    char status_msg[200];
    const char* state_name = RocketStateMachine_GetStateName(rocket.current_state);

    uint32_t time_in_state = HAL_GetTick() - rocket.state_start_time;
    uint32_t time_in_state_sec = time_in_state / 1000;

    // Convertir datos de vuelo a enteros para printf
    int32_t accel_x_int = (int32_t)(rocket.current_data.acceleration_x * 1000);
    int32_t accel_y_int = (int32_t)(rocket.current_data.acceleration_y * 1000);
    int32_t accel_z_int = (int32_t)(rocket.current_data.acceleration_z * 1000);
    int32_t pressure_int = (int32_t)(rocket.current_data.pressure * 100);
    int32_t alt_int = (int32_t)(rocket.current_data.altitude * 100);

    sprintf(status_msg, "ROCKET: %s (%lds) | Accel: %ld.%03d,%ld.%03d,%ld.%03d | Press: %ld.%02d | Alt: %ld.%02d | Data: %ld pts",
           state_name, time_in_state_sec,
           accel_x_int/1000, abs(accel_x_int%1000),
           accel_y_int/1000, abs(accel_y_int%1000),
           accel_z_int/1000, abs(accel_z_int%1000),
           pressure_int/100, abs(pressure_int%100),
           alt_int/100, abs(alt_int%100),
           rocket.total_data_points);

    SDLogger_WriteText(&sdlogger, status_msg);
}

/**
 * @brief Simula una secuencia de lanzamiento para testing
 */
static void SimulateLaunchSequence(void) {
    static uint32_t simulation_start = 0;
    static bool simulation_active = false;

    // Solo simular si la simulación está habilitada en configuración
    if (!rocket.config.simulation_mode_enabled) {
        return;
    }

    // Solo simular cuando estemos en ARMED por más de 5 segundos
    if (rocket.current_state == ROCKET_STATE_ARMED) {
        uint32_t time_armed = HAL_GetTick() - rocket.state_start_time;
        if (time_armed > 5000 && !simulation_active) {
            simulation_active = true;
            simulation_start = HAL_GetTick();
            // rocket.simulation_mode ya está configurado desde la carga de configuración
            // Simulation started - no logging to SD during flight
        }
    }

    if (simulation_active) {
        uint32_t sim_time = HAL_GetTick() - simulation_start;

        // Simular aceleración de lanzamiento por 3 segundos
        if (sim_time < 3000) {
            // Modificar temporalmente la lectura del acelerómetro (eje X)
            rocket.current_data.acceleration_x = 5.0f; // 5G de aceleración
        }
        // Simular fase de coast
        else if (sim_time < 8000) {
            rocket.current_data.acceleration_x = 1.0f; // Solo gravedad
            // Simular aumento de altitud
            rocket.current_data.altitude += 10.0f; // Subiendo rápido
        }
        // Simular apogeo y descenso
        else if (sim_time < 15000) {
            rocket.current_data.acceleration_x = 1.0f;
            // Simular descenso
            rocket.current_data.altitude -= 5.0f; // Bajando
        }
        // Simular aterrizaje - ALTITUD ESTABLE
        else {
            rocket.current_data.acceleration_x = 1.0f;
            // Mantener altitud estable para activar nueva condición de aterrizaje
            rocket.current_data.altitude = rocket.ground_altitude + 50.0f; // Estable a 50m
        }
    }
}

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

    // Delay inicial para estabilización
    HAL_Delay(1000);

    // Inicializar todos los sensores
    if (!InitializeAllSensors()) {
        while (1) {
            WS2812B_SetColorRGB(&led, 255, 0, 0); // Rojo = error crítico
            HAL_Delay(500);
            WS2812B_SetColorRGB(&led, 0, 0, 0);   // Apagado
            HAL_Delay(500);
        }
    }

    // Inicializar la máquina de estados del cohete
    if (!RocketStateMachine_Init(&rocket, &kx134, &ms5611, &gps, &led, &buzzer, &spiflash)) {
        BUZZER_ERROR(&buzzer);
        while (1) {
            WS2812B_SetColorRGB(&led, 255, 0, 0);
            HAL_Delay(1000);
        }
    }

    // Recuperación ya realizada durante inicialización temprana

    // Señal de inicialización completa
    WS2812B_SetColorRGB(&led, 0, 255, 255); // Cyan = inicializado
    BUZZER_SUCCESS(&buzzer);
    HAL_Delay(500);
    Buzzer_Pattern(&buzzer, BUZZER_PATTERN_SUCCESS);

    SDLogger_WriteText(&sdlogger, "=== ROCKET STATE MACHINE TEST INICIADO ===");
    SDLogger_WriteText(&sdlogger, "Estados: SLEEP->ARMED->BOOST->COAST->APOGEE->PARACHUTE->LANDED");
    SDLogger_WriteText(&sdlogger, "Colores LED: Morado=SLEEP, Amarillo=ARMED, Rojo=BOOST, Azul=COAST, Blanco=APOGEE, Cyan=PARACHUTE, Verde=LANDED");

    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        // PRIMERO: Simular secuencia de lanzamiento para testing (modifica datos antes de evaluarlos)
        SimulateLaunchSequence();

        // SEGUNDO: Actualizar máquina de estados del cohete (evalúa datos ya modificados)
        RocketStateMachine_Update(&rocket);

        // Debug status cada segundo
        uint32_t current_time = HAL_GetTick();
        // Debug status solo durante inicialización, no durante vuelo
        if (rocket.current_state == ROCKET_STATE_SLEEP || rocket.current_state == ROCKET_STATE_ARMED) {
            if (current_time - last_debug_time >= DEBUG_UPDATE_INTERVAL_MS) {
                PrintRocketStatus();
                last_debug_time = current_time;
            }
        }

        // Delay configurable desde SD
        HAL_Delay(rocket.config.data_logging_frequency_ms);

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
