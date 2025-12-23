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
    float temperature;  // °C
    float pressure;     // mbar
    int32_t altitude;   // metros (basado en presión estándar al nivel del mar)
} MS5611_Data_t;

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_gpio_port;
    uint16_t cs_pin;
    bool is_initialized;
    uint16_t calibration[8]; // C0-C7 (C0 no se usa, pero mantenemos índices consistentes)
    uint8_t osr; // Oversampling Ratio
} MS5611_t;

// Funciones públicas
bool MS5611_Init(MS5611_t* ms5611, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);
bool MS5611_Reset(MS5611_t* ms5611);
bool MS5611_ReadPROM(MS5611_t* ms5611);
bool MS5611_IsValidPROM(MS5611_t* ms5611);
bool MS5611_SetOSR(MS5611_t* ms5611, uint8_t osr);
uint32_t MS5611_ReadRawPressure(MS5611_t* ms5611);
uint32_t MS5611_ReadRawTemperature(MS5611_t* ms5611);
bool MS5611_ReadData(MS5611_t* ms5611, MS5611_Data_t *data);
float MS5611_CalculateTemperature(MS5611_t* ms5611, uint32_t D2);
float MS5611_CalculatePressure(MS5611_t* ms5611, uint32_t D1, uint32_t D2);
int32_t MS5611_CalculateAltitude(float pressure);
void MS5611_SendCommand(MS5611_t* ms5611, uint8_t cmd);
uint16_t MS5611_ReadPROMValue(MS5611_t* ms5611, uint8_t index);
uint32_t MS5611_ReadADC(MS5611_t* ms5611);

#ifdef __cplusplus
}
#endif

#endif // MS5611_H