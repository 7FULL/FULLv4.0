#include "MS5611.h"
#include <math.h>

static uint8_t MS5611_SPI_ReadWrite(MS5611_t* ms5611, uint8_t data) {
    uint8_t rx = 0;
    HAL_SPI_TransmitReceive(ms5611->hspi, &data, &rx, 1, HAL_MAX_DELAY);
    return rx;
}

bool MS5611_Init(MS5611_t* ms5611, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin) {
    if (!ms5611 || !hspi) return false;

    ms5611->hspi = hspi;
    ms5611->cs_gpio_port = cs_port;
    ms5611->cs_pin = cs_pin;
    ms5611->is_initialized = false;
    ms5611->osr = 0; // OSR=256 (0.6 ms conversion) — caller can change via MS5611_SetOSR

    // Non-blocking state machine initialisation
    ms5611->conv_state        = MS5611_CONV_IDLE;
    ms5611->conv_start_time_ms = 0;
    ms5611->raw_D1            = 0;
    ms5611->raw_D2            = 0;

    // Configurar CS como HIGH (inactivo)
    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_SET);
    HAL_Delay(50); // Delay más largo para estabilización

    // Reset del sensor con reintentos
    for (int i = 0; i < 3; i++) {
        if (MS5611_Reset(ms5611)) break;
        HAL_Delay(100);
        if (i == 2) return false; // Falló después de 3 intentos
    }

    // Leer coeficientes de calibración con reintentos
    for (int i = 0; i < 3; i++) {
        if (MS5611_ReadPROM(ms5611) && MS5611_IsValidPROM(ms5611)) {
            ms5611->is_initialized = true;
            return true;
        }
        HAL_Delay(100);
    }

    return false; // No se pudo inicializar correctamente
}

bool MS5611_Reset(MS5611_t* ms5611) {
    if (!ms5611) return false;

    MS5611_SendCommand(ms5611, MS5611_CMD_RESET);
    HAL_Delay(100); // Esperar reset completo

    return true;
}

void MS5611_SendCommand(MS5611_t* ms5611, uint8_t cmd) {
    if (!ms5611) return;

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_RESET);

    MS5611_SPI_ReadWrite(ms5611, cmd);

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_SET);
}

uint16_t MS5611_ReadPROMValue(MS5611_t* ms5611, uint8_t index) {
    if (!ms5611 || index > 7) return 0;

    uint8_t cmd = MS5611_CMD_PROM_READ + (index * 2);
    uint8_t msb, lsb;

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_RESET);

    MS5611_SPI_ReadWrite(ms5611, cmd);
    msb = MS5611_SPI_ReadWrite(ms5611, 0x00);
    lsb = MS5611_SPI_ReadWrite(ms5611, 0x00);

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_SET);

    return ((uint16_t)msb << 8) | lsb;
}

bool MS5611_ReadPROM(MS5611_t* ms5611) {
    if (!ms5611) return false;

    for (int i = 0; i < 8; i++) {
        ms5611->calibration[i] = MS5611_ReadPROMValue(ms5611, i);
        HAL_Delay(10);
    }

    return true;
}

bool MS5611_IsValidPROM(MS5611_t* ms5611) {
    if (!ms5611) return false;

    // Verificar que los coeficientes no sean todos 0x0000 o 0xFFFF
    for (int i = 1; i < 7; i++) { // C1-C6
        if (ms5611->calibration[i] == 0x0000 || ms5611->calibration[i] == 0xFFFF) {
            return false;
        }
    }

    // Verificar CRC si está implementado en el sensor
    // Por simplicidad, omitimos la verificación CRC aquí
    return true;
}

bool MS5611_SetOSR(MS5611_t* ms5611, uint8_t osr) {
    if (!ms5611 || !ms5611->is_initialized) return false;

    switch(osr) {
        case 0: // OSR=256
        case 1: // OSR=512
        case 2: // OSR=1024
        case 3: // OSR=2048
        case 4: // OSR=4096
            ms5611->osr = osr;
            return true;
        default:
            return false;
    }
}

uint32_t MS5611_ReadADC(MS5611_t* ms5611) {
    if (!ms5611) return 0;

    uint8_t data[3];

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_RESET);

    MS5611_SPI_ReadWrite(ms5611, MS5611_CMD_ADC_READ);
    data[0] = MS5611_SPI_ReadWrite(ms5611, 0x00); // MSB
    data[1] = MS5611_SPI_ReadWrite(ms5611, 0x00); // Mid
    data[2] = MS5611_SPI_ReadWrite(ms5611, 0x00); // LSB

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_SET);

    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

uint32_t MS5611_ReadRawPressure(MS5611_t* ms5611) {
    if (!ms5611 || !ms5611->is_initialized) return 0;

    uint8_t cmd = MS5611_CMD_CONVERT_D1_OSR256 + (ms5611->osr * 2);

    // Iniciar conversión
    MS5611_SendCommand(ms5611, cmd);

    // Esperar tiempo mínimo de conversión según OSR
    // No podemos hacer polling porque el MS5611 no tiene registro de estado
    uint32_t delay_us;
    switch(ms5611->osr) {
        case 0: delay_us = 600; break;    // OSR=256 (max 0.6ms)
        case 1: delay_us = 1200; break;   // OSR=512 (max 1.2ms)
        case 2: delay_us = 2300; break;   // OSR=1024 (max 2.3ms)
        case 3: delay_us = 4600; break;   // OSR=2048 (max 4.6ms)
        case 4: delay_us = 9100; break;   // OSR=4096 (max 9.1ms)
        default: delay_us = 2300; break;
    }

    // Use busy-wait for microsecond precision (HAL_Delay is 1ms resolution)
    uint32_t start = HAL_GetTick();
    uint32_t delay_ms = (delay_us + 999) / 1000;  // Round up to milliseconds
    while ((HAL_GetTick() - start) < delay_ms) {
        // Busy wait
    }

    // Leer resultado
    return MS5611_ReadADC(ms5611);
}

uint32_t MS5611_ReadRawTemperature(MS5611_t* ms5611) {
    if (!ms5611 || !ms5611->is_initialized) return 0;

    uint8_t cmd = MS5611_CMD_CONVERT_D2_OSR256 + (ms5611->osr * 2);

    // Iniciar conversión
    MS5611_SendCommand(ms5611, cmd);

    // Esperar tiempo mínimo de conversión según OSR
    // No podemos hacer polling porque el MS5611 no tiene registro de estado
    uint32_t delay_us;
    switch(ms5611->osr) {
        case 0: delay_us = 600; break;    // OSR=256 (max 0.6ms)
        case 1: delay_us = 1200; break;   // OSR=512 (max 1.2ms)
        case 2: delay_us = 2300; break;   // OSR=1024 (max 2.3ms)
        case 3: delay_us = 4600; break;   // OSR=2048 (max 4.6ms)
        case 4: delay_us = 9100; break;   // OSR=4096 (max 9.1ms)
        default: delay_us = 2300; break;
    }

    // Use busy-wait for microsecond precision (HAL_Delay is 1ms resolution)
    uint32_t start = HAL_GetTick();
    uint32_t delay_ms = (delay_us + 999) / 1000;  // Round up to milliseconds
    while ((HAL_GetTick() - start) < delay_ms) {
        // Busy wait
    }

    // Leer resultado
    return MS5611_ReadADC(ms5611);
}

float MS5611_CalculateTemperature(MS5611_t* ms5611, uint32_t D2) {
    if (!ms5611 || !ms5611->is_initialized) return 0.0f;

    int32_t dT = D2 - ((uint32_t)ms5611->calibration[5] << 8);
    int32_t TEMP = 2000 + ((int64_t)dT * ms5611->calibration[6] >> 23);

    return TEMP / 100.0f; // Convertir a grados Celsius
}

float MS5611_CalculatePressure(MS5611_t* ms5611, uint32_t D1, uint32_t D2) {
    if (!ms5611 || !ms5611->is_initialized) return 0.0f;

    // Cálculo de temperatura para compensación
    int32_t dT = D2 - ((uint32_t)ms5611->calibration[5] << 8);
    int32_t TEMP = 2000 + ((int64_t)dT * ms5611->calibration[6] >> 23);

    // Cálculo de presión compensada por temperatura
    int64_t OFF = ((int64_t)ms5611->calibration[2] << 16) + (((int64_t)ms5611->calibration[4] * dT) >> 7);
    int64_t SENS = ((int64_t)ms5611->calibration[1] << 15) + (((int64_t)ms5611->calibration[3] * dT) >> 8);

    // Compensación de segundo orden para bajas temperaturas
    if (TEMP < 2000) {
        int32_t T2 = (dT * dT) >> 31;
        int64_t OFF2 = 5 * ((TEMP - 2000) * (TEMP - 2000)) >> 1;
        int64_t SENS2 = 5 * ((TEMP - 2000) * (TEMP - 2000)) >> 2;

        if (TEMP < -1500) {
            OFF2 = OFF2 + 7 * ((TEMP + 1500) * (TEMP + 1500));
            SENS2 = SENS2 + 11 * ((TEMP + 1500) * (TEMP + 1500)) >> 1;
        }

        TEMP = TEMP - T2;
        OFF = OFF - OFF2;
        SENS = SENS - SENS2;
    }

    int32_t P = (((D1 * SENS) >> 21) - OFF) >> 15;

    return P / 100.0f; // Convertir a mbar
}

float MS5611_CalculateAltitude(float pressure) {
    // Barometric formula using standard sea-level pressure (1013.25 mbar)
    const float sea_level_pressure = 1013.25f;

    if (pressure <= 0.0f) return 0.0f;

    return 44330.0f * (1.0f - powf(pressure / sea_level_pressure, 1.0f / 5.255f));
}

// Returns the minimum conversion wait time in milliseconds for the given OSR index.
// Values are taken from the MS5611 datasheet max conversion times, rounded up to
// the next whole millisecond so HAL_GetTick() (1 ms resolution) is sufficient.
uint32_t MS5611_GetConversionTime_ms(uint8_t osr) {
    switch (osr) {
        case 0: return  1;  // OSR=256  : 0.60 ms max
        case 1: return  2;  // OSR=512  : 1.17 ms max
        case 2: return  3;  // OSR=1024 : 2.28 ms max
        case 3: return  5;  // OSR=2048 : 4.54 ms max
        case 4: return 10;  // OSR=4096 : 9.04 ms max
        default: return 3;
    }
}

// Non-blocking state machine.
// Call every loop iteration; returns true and fills *data only when a full
// pressure + temperature sample has been computed.
bool MS5611_Update(MS5611_t* ms5611, MS5611_Data_t* data) {
    if (!ms5611 || !ms5611->is_initialized || !data) return false;

    uint32_t now          = HAL_GetTick();
    uint32_t conv_time_ms = MS5611_GetConversionTime_ms(ms5611->osr);
    uint8_t  d1_cmd       = MS5611_CMD_CONVERT_D1_OSR256 + (ms5611->osr * 2);
    uint8_t  d2_cmd       = MS5611_CMD_CONVERT_D2_OSR256 + (ms5611->osr * 2);

    switch (ms5611->conv_state) {

        case MS5611_CONV_IDLE:
            // Start pressure (D1) conversion
            MS5611_SendCommand(ms5611, d1_cmd);
            ms5611->conv_start_time_ms = now;
            ms5611->conv_state = MS5611_CONV_D1;
            return false;

        case MS5611_CONV_D1:
            if ((now - ms5611->conv_start_time_ms) < conv_time_ms) return false;
            // D1 ready — read it, then kick off temperature (D2) conversion
            ms5611->raw_D1 = MS5611_ReadADC(ms5611);
            MS5611_SendCommand(ms5611, d2_cmd);
            ms5611->conv_start_time_ms = now;
            ms5611->conv_state = MS5611_CONV_D2;
            return false;

        case MS5611_CONV_D2:
            if ((now - ms5611->conv_start_time_ms) < conv_time_ms) return false;
            // D2 ready — read it and compute compensated values
            ms5611->raw_D2 = MS5611_ReadADC(ms5611);
            ms5611->conv_state = MS5611_CONV_IDLE;

            if (ms5611->raw_D1 == 0 || ms5611->raw_D2 == 0) return false;

            data->temperature = MS5611_CalculateTemperature(ms5611, ms5611->raw_D2);
            data->pressure    = MS5611_CalculatePressure(ms5611, ms5611->raw_D1, ms5611->raw_D2);
            data->altitude    = MS5611_CalculateAltitude(data->pressure);
            return true;
    }

    return false;
}

// Blocking one-shot read — for use during initialisation ONLY.
// In the flight loop always use MS5611_Update() instead.
bool MS5611_ReadData(MS5611_t* ms5611, MS5611_Data_t *data) {
    if (!ms5611 || !ms5611->is_initialized || !data) return false;

    uint32_t D1 = MS5611_ReadRawPressure(ms5611);
    uint32_t D2 = MS5611_ReadRawTemperature(ms5611);

    if (D1 == 0 || D2 == 0) return false;

    data->temperature = MS5611_CalculateTemperature(ms5611, D2);
    data->pressure    = MS5611_CalculatePressure(ms5611, D1, D2);
    data->altitude    = MS5611_CalculateAltitude(data->pressure);

    return true;
}