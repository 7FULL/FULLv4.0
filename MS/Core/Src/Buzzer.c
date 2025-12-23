#include "Buzzer.h"

bool Buzzer_Init(Buzzer_t *buzzer) {
    if (!buzzer) return false;

    buzzer->gpio_port = BUZZER_GPIO_PORT;
    buzzer->gpio_pin = BUZZER_PIN;
    buzzer->is_initialized = true;

    // Asegurar que el buzzer esté apagado al inicio
    Buzzer_Off(buzzer);

    return true;
}

void Buzzer_On(Buzzer_t *buzzer) {
    if (!buzzer || !buzzer->is_initialized) return;
    HAL_GPIO_WritePin(buzzer->gpio_port, buzzer->gpio_pin, GPIO_PIN_SET);
}

void Buzzer_Off(Buzzer_t *buzzer) {
    if (!buzzer || !buzzer->is_initialized) return;
    HAL_GPIO_WritePin(buzzer->gpio_port, buzzer->gpio_pin, GPIO_PIN_RESET);
}

bool Buzzer_IsOn(Buzzer_t *buzzer) {
    if (!buzzer || !buzzer->is_initialized) return false;
    return (HAL_GPIO_ReadPin(buzzer->gpio_port, buzzer->gpio_pin) == GPIO_PIN_SET);
}

void Buzzer_Beep(Buzzer_t *buzzer, uint16_t duration_ms) {
    if (!buzzer || !buzzer->is_initialized) return;

    Buzzer_On(buzzer);
    HAL_Delay(duration_ms);
    Buzzer_Off(buzzer);
}

void Buzzer_BeepType(Buzzer_t *buzzer, Buzzer_BeepType_t type) {
    if (!buzzer || !buzzer->is_initialized) return;

    switch (type) {
        case BUZZER_BEEP_SHORT:
            Buzzer_Beep(buzzer, 100);
            break;
        case BUZZER_BEEP_MEDIUM:
            Buzzer_Beep(buzzer, 300);
            break;
        case BUZZER_BEEP_LONG:
            Buzzer_Beep(buzzer, 500);
            break;
        case BUZZER_BEEP_VERY_LONG:
            Buzzer_Beep(buzzer, 1000);
            break;
    }
}

void Buzzer_BeepMultiple(Buzzer_t *buzzer, uint8_t count, uint16_t on_time_ms, uint16_t off_time_ms) {
    if (!buzzer || !buzzer->is_initialized) return;

    for (uint8_t i = 0; i < count; i++) {
        Buzzer_Beep(buzzer, on_time_ms);
        if (i < count - 1) { // No esperar después del último beep
            HAL_Delay(off_time_ms);
        }
    }
}

void Buzzer_Pattern(Buzzer_t *buzzer, Buzzer_Pattern_t pattern) {
    if (!buzzer || !buzzer->is_initialized) return;

    switch (pattern) {
        case BUZZER_PATTERN_SUCCESS:
            // 2 beeps cortos
            Buzzer_BeepMultiple(buzzer, 2, 200, 200);
            break;

        case BUZZER_PATTERN_ERROR:
            // 3 beeps rápidos
            Buzzer_BeepMultiple(buzzer, 3, 100, 100);
            break;

        case BUZZER_PATTERN_WARNING:
            // 1 beep largo
            Buzzer_BeepType(buzzer, BUZZER_BEEP_LONG);
            break;

        case BUZZER_PATTERN_INIT:
            // 1 beep medio
            Buzzer_BeepType(buzzer, BUZZER_BEEP_MEDIUM);
            break;

        case BUZZER_PATTERN_GPS_FIX:
            // 1 beep largo + pausa + 2 beeps cortos
            Buzzer_BeepType(buzzer, BUZZER_BEEP_LONG);
            HAL_Delay(300);
            Buzzer_BeepMultiple(buzzer, 2, 150, 150);
            break;

        case BUZZER_PATTERN_DATA_SAVED:
            // 2 beeps medios
            Buzzer_BeepMultiple(buzzer, 2, 300, 200);
            break;

        case BUZZER_PATTERN_STARTUP:
            // Secuencia ascendente: corto -> medio -> largo
            Buzzer_BeepType(buzzer, BUZZER_BEEP_SHORT);
            HAL_Delay(200);
            Buzzer_BeepType(buzzer, BUZZER_BEEP_MEDIUM);
            HAL_Delay(200);
            Buzzer_BeepType(buzzer, BUZZER_BEEP_LONG);
            break;
    }
}

void Buzzer_Melody(Buzzer_t *buzzer, uint16_t *durations, uint8_t length) {
    if (!buzzer || !buzzer->is_initialized || !durations) return;

    for (uint8_t i = 0; i < length; i++) {
        if (durations[i] > 0) {
            Buzzer_Beep(buzzer, durations[i]);
        } else {
            // Pausa (duración 0 = silencio de 100ms)
            HAL_Delay(100);
        }

        if (i < length - 1) {
            HAL_Delay(50); // Pequeña pausa entre notas
        }
    }
}