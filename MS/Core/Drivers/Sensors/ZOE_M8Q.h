#ifndef ZOE_M8Q_H
#define ZOE_M8Q_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "i2c.h"
#include <stdint.h>
#include <stdbool.h>

// Dirección I2C del ZOE-M8Q (7-bit address)
#define ZOE_M8Q_I2C_ADDR            0x42

// Pines de control GPIO
#define ZOE_M8Q_RESET_PIN           GPIO_PIN_8      // PC8
#define ZOE_M8Q_RESET_GPIO_PORT     GPIOC
#define ZOE_M8Q_IMPULSE_PIN         GPIO_PIN_10     // PA10
#define ZOE_M8Q_IMPULSE_GPIO_PORT   GPIOA

// Registros básicos
#define ZOE_M8Q_REG_DATA_STREAM     0xFF
#define ZOE_M8Q_REG_DATA_LENGTH_H   0xFD
#define ZOE_M8Q_REG_DATA_LENGTH_L   0xFE

// Estados del GPS
typedef enum {
    GPS_NO_FIX = 0,
    GPS_DEAD_RECKONING = 1,
    GPS_2D_FIX = 2,
    GPS_3D_FIX = 3,
    GPS_GNSS_DEAD_RECKONING = 4,
    GPS_TIME_ONLY = 5
} GPS_FixType_t;

// Estructura de datos GPS
typedef struct {
    // Posición
    double latitude;        // Grados decimales
    double longitude;       // Grados decimales
    float altitude;         // Metros sobre el nivel del mar

    // Tiempo
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;

    // Estado
    GPS_FixType_t fix_type;
    uint8_t satellites_used;
    float hdop;             // Dilución horizontal de precisión
    bool fix_valid;

    // Velocidad
    float speed_kmh;        // km/h
    float heading;          // Grados (0-360)

    // Timestamps
    uint32_t last_update;   // HAL_GetTick() del último update válido
} ZOE_M8Q_Data_t;

// Estructura principal del GPS
typedef struct {
    I2C_HandleTypeDef *hi2c;
    ZOE_M8Q_Data_t gps_data;
    bool is_initialized;
    char nmea_buffer[256];
    uint16_t buffer_index;
} ZOE_M8Q_t;

// Funciones públicas
bool ZOE_M8Q_Init(ZOE_M8Q_t *gps, I2C_HandleTypeDef *hi2c);
void ZOE_M8Q_Reset(void);
void ZOE_M8Q_SendImpulse(void);
bool ZOE_M8Q_IsDataAvailable(ZOE_M8Q_t *gps);
bool ZOE_M8Q_ReadData(ZOE_M8Q_t *gps);
bool ZOE_M8Q_ParseNMEA(ZOE_M8Q_t *gps, char *nmea_sentence);
bool ZOE_M8Q_GetLatestData(ZOE_M8Q_t *gps, ZOE_M8Q_Data_t *data_out);
bool ZOE_M8Q_HasValidFix(ZOE_M8Q_t *gps);
uint32_t ZOE_M8Q_GetTimeSinceLastUpdate(ZOE_M8Q_t *gps);

// Funciones de utilidad
void ZOE_M8Q_GetLocationString(ZOE_M8Q_Data_t *data, char *buffer, size_t buffer_size);
void ZOE_M8Q_GetTimeString(ZOE_M8Q_Data_t *data, char *buffer, size_t buffer_size);
void ZOE_M8Q_GetStatusString(ZOE_M8Q_Data_t *data, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // ZOE_M8Q_H