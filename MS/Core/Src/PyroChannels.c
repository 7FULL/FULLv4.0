#include "PyroChannels.h"

// Array simple para trackear estado de canales
static bool channel_states[4] = {false, false, false, false};

// Mapeo de canales a pines
static const struct {
    GPIO_TypeDef* gpio_port;
    uint16_t pin;
} channel_pins[4] = {
    {PYRO_CH1_GPIO_PORT, PYRO_CH1_PIN},  // Canal 0
    {PYRO_CH2_GPIO_PORT, PYRO_CH2_PIN},  // Canal 1
    {PYRO_CH3_GPIO_PORT, PYRO_CH3_PIN},  // Canal 2
    {PYRO_CH4_GPIO_PORT, PYRO_CH4_PIN}   // Canal 3
};

void PyroChannels_Init(void) {
    // Inicializar todos los canales como OFF
    PyroChannels_DeactivateAll();
}

void PyroChannels_ActivateChannel(uint8_t channel) {
    if (channel >= 4) return;  // Validación simple

    HAL_GPIO_WritePin(channel_pins[channel].gpio_port,
                     channel_pins[channel].pin,
                     GPIO_PIN_SET);
    channel_states[channel] = true;
}

void PyroChannels_DeactivateChannel(uint8_t channel) {
    if (channel >= 4) return;  // Validación simple

    HAL_GPIO_WritePin(channel_pins[channel].gpio_port,
                     channel_pins[channel].pin,
                     GPIO_PIN_RESET);
    channel_states[channel] = false;
}

void PyroChannels_ActivateAll(void) {
    for (uint8_t i = 0; i < 4; i++) {
        PyroChannels_ActivateChannel(i);
    }
}

void PyroChannels_DeactivateAll(void) {
    for (uint8_t i = 0; i < 4; i++) {
        PyroChannels_DeactivateChannel(i);
    }
}

bool PyroChannels_IsChannelActive(uint8_t channel) {
    if (channel >= 4) return false;

    return channel_states[channel];
}