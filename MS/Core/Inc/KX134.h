#ifndef KX134_H
#define KX134_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "spi.h"
#include <stdint.h>
#include <stdbool.h>

// Registros del KX134
#define KX134_WHO_AM_I          0x13
#define KX134_CNTL1             0x1B
#define KX134_CNTL2             0x1C
#define KX134_CNTL3             0x1D
#define KX134_ODCNTL            0x1F
#define KX134_INC1              0x20
#define KX134_INC4              0x23
#define KX134_TILT_TIMER        0x29
#define KX134_TDTRC             0x2A
#define KX134_TDTC              0x2B
#define KX134_TTH               0x2C
#define KX134_TTL               0x2D
#define KX134_FTD               0x2E
#define KX134_STD               0x2F
#define KX134_TLT               0x30
#define KX134_TWS               0x31
#define KX134_FFTH              0x32
#define KX134_FFC               0x33
#define KX134_FFCNTL            0x34
#define KX134_TILT_ANGLE_LL     0x37
#define KX134_TILT_ANGLE_HL     0x38
#define KX134_HYST_SET          0x39
#define KX134_LP_CNTL1          0x3A
#define KX134_LP_CNTL2          0x3B
#define KX134_WUFTH             0x40
#define KX134_BTSWUFTH          0x41
#define KX134_BTSTH             0x42
#define KX134_BTSC              0x43
#define KX134_WUFC              0x44
#define KX134_XOUT_L            0x08
#define KX134_XOUT_H            0x09
#define KX134_YOUT_L            0x0A
#define KX134_YOUT_H            0x0B
#define KX134_ZOUT_L            0x0C
#define KX134_ZOUT_H            0x0D

// Valores esperados
#define KX134_DEVICE_ID         0x46

// Configuración
#define KX134_CS_PIN            GPIO_PIN_1
#define KX134_CS_GPIO_PORT      GPIOB

typedef struct {
    float x;
    float y;
    float z;
} KX134_AccelData_t;

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_gpio_port;
    uint16_t cs_pin;
    bool is_initialized;
    uint8_t range; // ±8g=0, ±16g=1, ±32g=2, ±64g=3
} KX134_t;

// Funciones públicas
bool KX134_Init(KX134_t* kx134, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);
bool KX134_CheckID(KX134_t* kx134);
bool KX134_Configure(KX134_t* kx134, uint8_t range);
bool KX134_Enable(KX134_t* kx134);
bool KX134_Disable(KX134_t* kx134);
uint8_t KX134_ReadRegister(KX134_t* kx134, uint8_t reg);
bool KX134_WriteRegister(KX134_t* kx134, uint8_t reg, uint8_t value);
bool KX134_ReadAccelRaw(KX134_t* kx134, int16_t *x, int16_t *y, int16_t *z);
bool KX134_ReadAccelG(KX134_t* kx134, KX134_AccelData_t *accel);
float KX134_ConvertToG(int16_t raw_value, uint8_t range);

#ifdef __cplusplus
}
#endif

#endif // KX134_H
