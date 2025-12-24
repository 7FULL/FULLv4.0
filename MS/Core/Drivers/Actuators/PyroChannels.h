#ifndef PYROCHANNELS_H
#define PYROCHANNELS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "gpio.h"
#include <stdint.h>
#include <stdbool.h>

// Definición de pines - AO3400A MOSFETs N-channel
#define PYRO_CH1_PIN            GPIO_PIN_3      // PC3
#define PYRO_CH1_GPIO_PORT      GPIOC
#define PYRO_CH2_PIN            GPIO_PIN_2      // PC2
#define PYRO_CH2_GPIO_PORT      GPIOC
#define PYRO_CH3_PIN            GPIO_PIN_1      // PC1
#define PYRO_CH3_GPIO_PORT      GPIOC
#define PYRO_CH4_PIN            GPIO_PIN_9      // PB9
#define PYRO_CH4_GPIO_PORT      GPIOB

// Funciones simples y directas
void PyroChannels_Init(void);
void PyroChannels_ActivateChannel(uint8_t channel);
void PyroChannels_DeactivateChannel(uint8_t channel);
void PyroChannels_ActivateAll(void);
void PyroChannels_DeactivateAll(void);
bool PyroChannels_IsChannelActive(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif // PYROCHANNELS_H