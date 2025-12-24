#ifndef BUZZER_H
#define BUZZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

// Configuración del buzzer
#define BUZZER_PIN              BUZZER_Pin
#define BUZZER_GPIO_PORT        BUZZER_GPIO_Port

// Tipos de sonidos predefinidos
typedef enum {
    BUZZER_BEEP_SHORT = 0,      // Beep corto (100ms)
    BUZZER_BEEP_MEDIUM,         // Beep medio (300ms)
    BUZZER_BEEP_LONG,           // Beep largo (500ms)
    BUZZER_BEEP_VERY_LONG       // Beep muy largo (1000ms)
} Buzzer_BeepType_t;

// Patrones de sonido predefinidos
typedef enum {
    BUZZER_PATTERN_SUCCESS = 0, // 2 beeps cortos
    BUZZER_PATTERN_ERROR,       // 3 beeps rápidos
    BUZZER_PATTERN_WARNING,     // 1 beep largo
    BUZZER_PATTERN_INIT,        // 1 beep medio
    BUZZER_PATTERN_GPS_FIX,     // 1 beep largo + 2 cortos
    BUZZER_PATTERN_DATA_SAVED,  // 2 beeps medios
    BUZZER_PATTERN_STARTUP      // Secuencia de inicio
} Buzzer_Pattern_t;

// Estructura principal del buzzer
typedef struct {
    bool is_initialized;
    GPIO_TypeDef* gpio_port;
    uint16_t gpio_pin;
} Buzzer_t;

// Funciones públicas
bool Buzzer_Init(Buzzer_t *buzzer);
void Buzzer_Beep(Buzzer_t *buzzer, uint16_t duration_ms);
void Buzzer_BeepType(Buzzer_t *buzzer, Buzzer_BeepType_t type);
void Buzzer_Pattern(Buzzer_t *buzzer, Buzzer_Pattern_t pattern);
void Buzzer_BeepMultiple(Buzzer_t *buzzer, uint8_t count, uint16_t on_time_ms, uint16_t off_time_ms);
void Buzzer_Melody(Buzzer_t *buzzer, uint16_t *durations, uint8_t length);

// Funciones de utilidad
void Buzzer_On(Buzzer_t *buzzer);
void Buzzer_Off(Buzzer_t *buzzer);
bool Buzzer_IsOn(Buzzer_t *buzzer);

// Macros de conveniencia
#define BUZZER_SUCCESS(buzzer)      Buzzer_Pattern(buzzer, BUZZER_PATTERN_SUCCESS)
#define BUZZER_ERROR(buzzer)        Buzzer_Pattern(buzzer, BUZZER_PATTERN_ERROR)
#define BUZZER_WARNING(buzzer)      Buzzer_Pattern(buzzer, BUZZER_PATTERN_WARNING)
#define BUZZER_INIT_OK(buzzer)      Buzzer_Pattern(buzzer, BUZZER_PATTERN_INIT)
#define BUZZER_GPS_FIX(buzzer)      Buzzer_Pattern(buzzer, BUZZER_PATTERN_GPS_FIX)

#ifdef __cplusplus
}
#endif

#endif // BUZZER_H