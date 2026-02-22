#ifndef MS5611_H
#define MS5611_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "spi.h"
#include <stdint.h>
#include <stdbool.h>

// Comandos del MS5611
#define MS5611_CMD_RESET            0x1E
#define MS5611_CMD_CONVERT_D1_OSR256    0x40
#define MS5611_CMD_CONVERT_D1_OSR512    0x42
#define MS5611_CMD_CONVERT_D1_OSR1024   0x44
#define MS5611_CMD_CONVERT_D1_OSR2048   0x46
#define MS5611_CMD_CONVERT_D1_OSR4096   0x48
#define MS5611_CMD_CONVERT_D2_OSR256    0x50
#define MS5611_CMD_CONVERT_D2_OSR512    0x52
#define MS5611_CMD_CONVERT_D2_OSR1024   0x54
#define MS5611_CMD_CONVERT_D2_OSR2048   0x56
#define MS5611_CMD_CONVERT_D2_OSR4096   0x58
#define MS5611_CMD_ADC_READ         0x00
#define MS5611_CMD_PROM_READ        0xA0

// Configuración
#define MS5611_CS_PIN               GPIO_PIN_4
#define MS5611_CS_GPIO_PORT         GPIOC

typedef struct {
    float temperature;  // Celsius
    float pressure;     // mbar
    float altitude;     // meters MSL (standard sea-level reference)
} MS5611_Data_t;

// Internal state for non-blocking conversion cycle
typedef enum {
    MS5611_CONV_IDLE = 0,   // Ready to start a new conversion
    MS5611_CONV_D1,         // Waiting for pressure (D1) conversion to complete
    MS5611_CONV_D2          // Waiting for temperature (D2) conversion to complete
} MS5611_ConvState_t;

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_gpio_port;
    uint16_t cs_pin;
    bool is_initialized;
    uint16_t calibration[8]; // C0-C7 (C0 unused, indices kept consistent)
    uint8_t osr;             // Oversampling Ratio index (0=OSR256 ... 4=OSR4096)

    // Non-blocking conversion state
    MS5611_ConvState_t conv_state;
    uint32_t conv_start_time_ms;
    uint32_t raw_D1;
    uint32_t raw_D2;
} MS5611_t;

// Public functions
bool MS5611_Init(MS5611_t* ms5611, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);
bool MS5611_Reset(MS5611_t* ms5611);
bool MS5611_ReadPROM(MS5611_t* ms5611);
bool MS5611_IsValidPROM(MS5611_t* ms5611);
bool MS5611_SetOSR(MS5611_t* ms5611, uint8_t osr);

// Returns the minimum conversion wait time in ms for the current OSR setting.
// Used internally by MS5611_Update; exposed so callers can verify timing budget.
uint32_t MS5611_GetConversionTime_ms(uint8_t osr);

// Non-blocking update — call every loop iteration.
// Returns true (and fills *data) only when a complete P+T sample is ready.
// Never blocks; all waiting is deferred to subsequent calls.
bool MS5611_Update(MS5611_t* ms5611, MS5611_Data_t *data);

// Blocking one-shot read — use ONLY during initialisation, never in the flight loop.
bool MS5611_ReadData(MS5611_t* ms5611, MS5611_Data_t *data);

float MS5611_CalculateTemperature(MS5611_t* ms5611, uint32_t D2);
float MS5611_CalculatePressure(MS5611_t* ms5611, uint32_t D1, uint32_t D2);
float MS5611_CalculateAltitude(float pressure);
void MS5611_SendCommand(MS5611_t* ms5611, uint8_t cmd);
uint16_t MS5611_ReadPROMValue(MS5611_t* ms5611, uint8_t index);
uint32_t MS5611_ReadADC(MS5611_t* ms5611);
uint32_t MS5611_ReadRawPressure(MS5611_t* ms5611);
uint32_t MS5611_ReadRawTemperature(MS5611_t* ms5611);

#ifdef __cplusplus
}
#endif

#endif // MS5611_H