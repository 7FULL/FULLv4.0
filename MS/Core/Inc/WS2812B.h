#ifndef WS2812B_H
#define WS2812B_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "tim.h"
#include <stdint.h>
#include <stdbool.h>

// Pin de control del WS2812B
#define WS2812B_PIN             GPIO_PIN_9      // PA9
#define WS2812B_GPIO_PORT       GPIOA

// Configuración del protocolo WS2812B
#define WS2812B_RESET_PULSE     50              // Reset pulse en microsegundos (>50us)
#define WS2812B_FREQUENCY       800000          // 800kHz
#define WS2812B_BITS_PER_LED    24              // 8 bits por color (GRB)

// Valores PWM para codificar bits (asumiendo ARR = 99 para 800kHz con 80MHz)
#define WS2812B_0_CODE          33              // ~33% duty cycle para bit '0' (33/99)
#define WS2812B_1_CODE          66              // ~67% duty cycle para bit '1' (66/99)

// Estructura de color RGB
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} WS2812B_Color_t;

// Colores predefinidos
#define WS2812B_COLOR_OFF       (WS2812B_Color_t){0, 0, 0}
#define WS2812B_COLOR_RED       (WS2812B_Color_t){255, 0, 0}
#define WS2812B_COLOR_GREEN     (WS2812B_Color_t){0, 255, 0}
#define WS2812B_COLOR_BLUE      (WS2812B_Color_t){0, 0, 255}
#define WS2812B_COLOR_WHITE     (WS2812B_Color_t){255, 255, 255}
#define WS2812B_COLOR_YELLOW    (WS2812B_Color_t){255, 255, 0}
#define WS2812B_COLOR_CYAN      (WS2812B_Color_t){0, 255, 255}
#define WS2812B_COLOR_MAGENTA   (WS2812B_Color_t){255, 0, 255}
#define WS2812B_COLOR_ORANGE    (WS2812B_Color_t){255, 165, 0}

// Estructura principal del WS2812B
typedef struct {
    TIM_HandleTypeDef *htim;        // Timer para PWM
    uint32_t channel;               // Canal del timer
    bool is_initialized;
    uint16_t pwm_buffer[24 + 50];   // Buffer PWM: 24 bits + reset
    WS2812B_Color_t current_color;
} WS2812B_t;

// Funciones públicas
bool WS2812B_Init(WS2812B_t *led, TIM_HandleTypeDef *htim, uint32_t channel);
bool WS2812B_SetColor(WS2812B_t *led, WS2812B_Color_t color);
bool WS2812B_SetColorRGB(WS2812B_t *led, uint8_t red, uint8_t green, uint8_t blue);
bool WS2812B_TurnOff(WS2812B_t *led);
bool WS2812B_SetBrightness(WS2812B_t *led, WS2812B_Color_t color, float brightness);
WS2812B_Color_t WS2812B_GetCurrentColor(WS2812B_t *led);

// Funciones de efectos
bool WS2812B_Blink(WS2812B_t *led, WS2812B_Color_t color, uint16_t on_time_ms, uint16_t off_time_ms, uint8_t blinks);
bool WS2812B_Pulse(WS2812B_t *led, WS2812B_Color_t color, uint16_t duration_ms);

// Funciones de utilidad
WS2812B_Color_t WS2812B_HSVToRGB(uint16_t hue, uint8_t saturation, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif // WS2812B_H