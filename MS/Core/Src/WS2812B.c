#include "WS2812B.h"
#include <string.h>

// Función para codificar un color en el buffer PWM
static void WS2812B_EncodeColor(WS2812B_t *led, WS2812B_Color_t color) {
    uint32_t grb_data = 0;

    // WS2812B usa orden GRB (Green-Red-Blue), no RGB
    grb_data = (color.green << 16) | (color.red << 8) | color.blue;

    // Codificar 24 bits en valores PWM
    for (int i = 0; i < 24; i++) {
        if (grb_data & (1 << (23 - i))) {
            led->pwm_buffer[i] = WS2812B_1_CODE;  // Bit '1'
        } else {
            led->pwm_buffer[i] = WS2812B_0_CODE;  // Bit '0'
        }
    }

    // Añadir pulso de reset (50 períodos en LOW = 0)
    for (int i = 24; i < 24 + 50; i++) {
        led->pwm_buffer[i] = 0;
    }
}

// Transmitir datos via PWM+DMA
static bool WS2812B_Transmit(WS2812B_t *led) {
    if (!led) return false;

    // Iniciar PWM con DMA
    HAL_StatusTypeDef status = HAL_TIM_PWM_Start_DMA(led->htim, led->channel,
                              (uint32_t*)led->pwm_buffer,
                              sizeof(led->pwm_buffer) / sizeof(uint16_t));

    // Para debug: el status se puede verificar externamente
    return (status == HAL_OK);
}

bool WS2812B_Init(WS2812B_t *led, TIM_HandleTypeDef *htim, uint32_t channel) {
    if (!led || !htim) return false;

    led->htim = htim;
    led->channel = channel;
    led->current_color = WS2812B_COLOR_OFF;
    led->is_initialized = false;

    // Limpiar buffer
    memset(led->pwm_buffer, 0, sizeof(led->pwm_buffer));

    // El pin PA9 ya está configurado por CubeMX como TIM1_CH2
    // No necesitamos reconfigurarlo aquí

    // Test inicial - apagar LED
    WS2812B_EncodeColor(led, WS2812B_COLOR_OFF);
    if (!WS2812B_Transmit(led)) {
        return false;
    }

    led->is_initialized = true;
    return true;
}

bool WS2812B_SetColor(WS2812B_t *led, WS2812B_Color_t color) {
    if (!led || !led->is_initialized) return false;

    led->current_color = color;
    WS2812B_EncodeColor(led, color);
    return WS2812B_Transmit(led);
}

bool WS2812B_SetColorRGB(WS2812B_t *led, uint8_t red, uint8_t green, uint8_t blue) {
    WS2812B_Color_t color = {red, green, blue};
    return WS2812B_SetColor(led, color);
}

bool WS2812B_TurnOff(WS2812B_t *led) {
    return WS2812B_SetColor(led, WS2812B_COLOR_OFF);
}

bool WS2812B_SetBrightness(WS2812B_t *led, WS2812B_Color_t color, float brightness) {
    if (!led || brightness < 0.0f || brightness > 1.0f) return false;

    WS2812B_Color_t dimmed_color;
    dimmed_color.red = (uint8_t)(color.red * brightness);
    dimmed_color.green = (uint8_t)(color.green * brightness);
    dimmed_color.blue = (uint8_t)(color.blue * brightness);

    return WS2812B_SetColor(led, dimmed_color);
}

WS2812B_Color_t WS2812B_GetCurrentColor(WS2812B_t *led) {
    if (!led) return WS2812B_COLOR_OFF;
    return led->current_color;
}

bool WS2812B_Blink(WS2812B_t *led, WS2812B_Color_t color, uint16_t on_time_ms, uint16_t off_time_ms, uint8_t blinks) {
    if (!led || !led->is_initialized) return false;

    for (uint8_t i = 0; i < blinks; i++) {
        // Encender
        if (!WS2812B_SetColor(led, color)) return false;
        HAL_Delay(on_time_ms);

        // Apagar
        if (!WS2812B_TurnOff(led)) return false;
        if (i < blinks - 1) { // No esperar después del último parpadeo
            HAL_Delay(off_time_ms);
        }
    }

    return true;
}

bool WS2812B_Pulse(WS2812B_t *led, WS2812B_Color_t color, uint16_t duration_ms) {
    if (!led || !led->is_initialized) return false;

    const uint8_t steps = 50;
    const uint16_t step_delay = duration_ms / (2 * steps);

    // Fade in
    for (uint8_t i = 0; i <= steps; i++) {
        float brightness = (float)i / steps;
        if (!WS2812B_SetBrightness(led, color, brightness)) return false;
        HAL_Delay(step_delay);
    }

    // Fade out
    for (uint8_t i = steps; i > 0; i--) {
        float brightness = (float)i / steps;
        if (!WS2812B_SetBrightness(led, color, brightness)) return false;
        HAL_Delay(step_delay);
    }

    return WS2812B_TurnOff(led);
}

WS2812B_Color_t WS2812B_HSVToRGB(uint16_t hue, uint8_t saturation, uint8_t value) {
    WS2812B_Color_t color = {0, 0, 0};

    if (saturation == 0) {
        color.red = color.green = color.blue = value;
        return color;
    }

    uint8_t region = hue / 60;
    uint8_t remainder = (hue % 60) * 255 / 60;

    uint8_t p = (value * (255 - saturation)) / 255;
    uint8_t q = (value * (255 - ((saturation * remainder) / 255))) / 255;
    uint8_t t = (value * (255 - ((saturation * (255 - remainder)) / 255))) / 255;

    switch (region) {
        case 0:
            color.red = value; color.green = t; color.blue = p;
            break;
        case 1:
            color.red = q; color.green = value; color.blue = p;
            break;
        case 2:
            color.red = p; color.green = value; color.blue = t;
            break;
        case 3:
            color.red = p; color.green = q; color.blue = value;
            break;
        case 4:
            color.red = t; color.green = p; color.blue = value;
            break;
        default:
            color.red = value; color.green = p; color.blue = q;
            break;
    }

    return color;
}