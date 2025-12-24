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
    ms5611->osr = 4; // OSR=4096 por defecto (mejor resolución)

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
    HAL_Delay(1);

    MS5611_SPI_ReadWrite(ms5611, cmd);

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_SET);
    HAL_Delay(1);
}

uint16_t MS5611_ReadPROMValue(MS5611_t* ms5611, uint8_t index) {
    if (!ms5611 || index > 7) return 0;

    uint8_t cmd = MS5611_CMD_PROM_READ + (index * 2);
    uint8_t msb, lsb;

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_RESET);
    HAL_Delay(1);

    MS5611_SPI_ReadWrite(ms5611, cmd);
    msb = MS5611_SPI_ReadWrite(ms5611, 0x00);
    lsb = MS5611_SPI_ReadWrite(ms5611, 0x00);

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_SET);
    HAL_Delay(1);

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
    HAL_Delay(1);

    MS5611_SPI_ReadWrite(ms5611, MS5611_CMD_ADC_READ);
    data[0] = MS5611_SPI_ReadWrite(ms5611, 0x00); // MSB
    data[1] = MS5611_SPI_ReadWrite(ms5611, 0x00); // Mid
    data[2] = MS5611_SPI_ReadWrite(ms5611, 0x00); // LSB

    HAL_GPIO_WritePin(ms5611->cs_gpio_port, ms5611->cs_pin, GPIO_PIN_SET);
    HAL_Delay(1);

    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

uint32_t MS5611_ReadRawPressure(MS5611_t* ms5611) {
    if (!ms5611 || !ms5611->is_initialized) return 0;

    uint8_t cmd = MS5611_CMD_CONVERT_D1_OSR256 + (ms5611->osr * 2);

    // Iniciar conversión
    MS5611_SendCommand(ms5611, cmd);

    // Esperar conversión según OSR
    uint16_t delay_ms;
    switch(ms5611->osr) {
        case 0: delay_ms = 1; break;   // OSR=256
        case 1: delay_ms = 2; break;   // OSR=512
        case 2: delay_ms = 3; break;   // OSR=1024
        case 3: delay_ms = 5; break;   // OSR=2048
        case 4: delay_ms = 10; break;  // OSR=4096
        default: delay_ms = 10; break;
    }
    HAL_Delay(delay_ms);

    // Leer resultado
    return MS5611_ReadADC(ms5611);
}

uint32_t MS5611_ReadRawTemperature(MS5611_t* ms5611) {
    if (!ms5611 || !ms5611->is_initialized) return 0;

    uint8_t cmd = MS5611_CMD_CONVERT_D2_OSR256 + (ms5611->osr * 2);

    // Iniciar conversión
    MS5611_SendCommand(ms5611, cmd);

    // Esperar conversión según OSR
    uint16_t delay_ms;
    switch(ms5611->osr) {
        case 0: delay_ms = 1; break;   // OSR=256
        case 1: delay_ms = 2; break;   // OSR=512
        case 2: delay_ms = 3; break;   // OSR=1024
        case 3: delay_ms = 5; break;   // OSR=2048
        case 4: delay_ms = 10; break;  // OSR=4096
        default: delay_ms = 10; break;
    }
    HAL_Delay(delay_ms);

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

int32_t MS5611_CalculateAltitude(float pressure) {
    // Fórmula barométrica usando presión estándar al nivel del mar (1013.25 mbar)
    const float sea_level_pressure = 1013.25f;

    if (pressure <= 0) return 0;

    float altitude = 44330.0f * (1.0f - powf(pressure / sea_level_pressure, 1.0f / 5.255f));
    return (int32_t)altitude;
}

bool MS5611_ReadData(MS5611_t* ms5611, MS5611_Data_t *data) {
    if (!ms5611 || !ms5611->is_initialized || !data) return false;

    // Leer datos raw
    uint32_t D1 = MS5611_ReadRawPressure(ms5611);
    uint32_t D2 = MS5611_ReadRawTemperature(ms5611);

    if (D1 == 0 || D2 == 0) return false;

    // Calcular valores compensados
    data->temperature = MS5611_CalculateTemperature(ms5611, D2);
    data->pressure = MS5611_CalculatePressure(ms5611, D1, D2);
    data->altitude = MS5611_CalculateAltitude(data->pressure);

    return true;
}