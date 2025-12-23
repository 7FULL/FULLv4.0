#include "KX134.h"

static uint8_t KX134_SPI_ReadWrite(KX134_t* kx134, uint8_t data) {
    uint8_t rx = 0;
    HAL_SPI_TransmitReceive(kx134->hspi, &data, &rx, 1, HAL_MAX_DELAY);
    return rx;
}

bool KX134_Init(KX134_t* kx134, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin) {
    if (!kx134 || !hspi) return false;

    kx134->hspi = hspi;
    kx134->cs_gpio_port = cs_port;
    kx134->cs_pin = cs_pin;
    kx134->is_initialized = false;
    kx134->range = 0; // ±8g por defecto

    // Configurar CS como HIGH (inactivo)
    HAL_GPIO_WritePin(kx134->cs_gpio_port, kx134->cs_pin, GPIO_PIN_SET);
    HAL_Delay(50); // Delay más largo para estabilización

    // Realizar soft reset antes de la inicialización
    KX134_WriteRegister(kx134, KX134_CNTL2, 0x80); // Software reset
    HAL_Delay(100);

    kx134->is_initialized = true;
    return true;
}

bool KX134_CheckID(KX134_t* kx134) {
    if (!kx134 || !kx134->is_initialized) return false;

    uint8_t device_id = KX134_ReadRegister(kx134, KX134_WHO_AM_I);
    return (device_id == KX134_DEVICE_ID);
}

uint8_t KX134_ReadRegister(KX134_t* kx134, uint8_t reg) {
    if (!kx134 || !kx134->is_initialized) return 0;

    HAL_GPIO_WritePin(kx134->cs_gpio_port, kx134->cs_pin, GPIO_PIN_RESET);
    HAL_Delay(1);

    KX134_SPI_ReadWrite(kx134, reg | 0x80); // Bit 7 = 1 para lectura
    uint8_t value = KX134_SPI_ReadWrite(kx134, 0x00);

    HAL_GPIO_WritePin(kx134->cs_gpio_port, kx134->cs_pin, GPIO_PIN_SET);
    HAL_Delay(1);

    return value;
}

bool KX134_WriteRegister(KX134_t* kx134, uint8_t reg, uint8_t value) {
    if (!kx134 || !kx134->is_initialized) return false;

    HAL_GPIO_WritePin(kx134->cs_gpio_port, kx134->cs_pin, GPIO_PIN_RESET);
    HAL_Delay(1);

    KX134_SPI_ReadWrite(kx134, reg); // Bit 7 = 0 para escritura
    KX134_SPI_ReadWrite(kx134, value);

    HAL_GPIO_WritePin(kx134->cs_gpio_port, kx134->cs_pin, GPIO_PIN_SET);
    HAL_Delay(1);

    return true;
}

bool KX134_Configure(KX134_t* kx134, uint8_t range) {
    if (!kx134 || !kx134->is_initialized) return false;
    if (range > 3) return false; // Rango válido: 0-3

    // Deshabilitar primero
    KX134_WriteRegister(kx134, KX134_CNTL1, 0x00);
    HAL_Delay(2);

    kx134->range = range;

    // Configurar CNTL1: Range y resolución
    uint8_t cntl1_val = 0x00;
    switch(range) {
        case 0: cntl1_val = 0x08; break; // ±8g
        case 1: cntl1_val = 0x10; break; // ±16g
        case 2: cntl1_val = 0x18; break; // ±32g
        case 3: cntl1_val = 0x20; break; // ±64g
    }
    KX134_WriteRegister(kx134, KX134_CNTL1, cntl1_val);

    // Configurar ODR (Output Data Rate) - 50Hz
    KX134_WriteRegister(kx134, KX134_ODCNTL, 0x02);

    HAL_Delay(10);
    return true;
}

bool KX134_Enable(KX134_t* kx134) {
    if (!kx134 || !kx134->is_initialized) return false;

    // Leer valor actual de CNTL1 y activar bit PC1 (bit 7)
    uint8_t cntl1_val = KX134_ReadRegister(kx134, KX134_CNTL1);
    cntl1_val |= 0x80; // Set PC1 bit

    KX134_WriteRegister(kx134, KX134_CNTL1, cntl1_val);
    HAL_Delay(10);

    return true;
}

bool KX134_Disable(KX134_t* kx134) {
    if (!kx134 || !kx134->is_initialized) return false;

    // Leer valor actual de CNTL1 y desactivar bit PC1 (bit 7)
    uint8_t cntl1_val = KX134_ReadRegister(kx134, KX134_CNTL1);
    cntl1_val &= ~0x80; // Clear PC1 bit

    KX134_WriteRegister(kx134, KX134_CNTL1, cntl1_val);
    HAL_Delay(2);

    return true;
}

bool KX134_ReadAccelRaw(KX134_t* kx134, int16_t *x, int16_t *y, int16_t *z) {
    if (!kx134 || !kx134->is_initialized || !x || !y || !z) return false;

    uint8_t data[6];

    // Leer los 6 bytes consecutivos de datos de aceleración
    HAL_GPIO_WritePin(kx134->cs_gpio_port, kx134->cs_pin, GPIO_PIN_RESET);
    HAL_Delay(1);

    KX134_SPI_ReadWrite(kx134, KX134_XOUT_L | 0x80); // Lectura múltiple

    for(int i = 0; i < 6; i++) {
        data[i] = KX134_SPI_ReadWrite(kx134, 0x00);
    }

    HAL_GPIO_WritePin(kx134->cs_gpio_port, kx134->cs_pin, GPIO_PIN_SET);
    HAL_Delay(1);

    // Combinar bytes (little endian)
    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);

    return true;
}

float KX134_ConvertToG(int16_t raw_value, uint8_t range) {
    float scale;

    switch(range) {
        case 0: scale = 8.0f / 32768.0f; break;   // ±8g
        case 1: scale = 16.0f / 32768.0f; break;  // ±16g
        case 2: scale = 32.0f / 32768.0f; break;  // ±32g
        case 3: scale = 64.0f / 32768.0f; break;  // ±64g
        default: scale = 8.0f / 32768.0f; break;
    }

    return (float)raw_value * scale;
}

bool KX134_ReadAccelG(KX134_t* kx134, KX134_AccelData_t *accel) {
    if (!kx134 || !accel) return false;

    int16_t raw_x, raw_y, raw_z;

    if (!KX134_ReadAccelRaw(kx134, &raw_x, &raw_y, &raw_z)) {
        return false;
    }

    accel->x = KX134_ConvertToG(raw_x, kx134->range);
    accel->y = KX134_ConvertToG(raw_y, kx134->range);
    accel->z = KX134_ConvertToG(raw_z, kx134->range);

    return true;
}