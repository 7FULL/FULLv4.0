#ifndef SERVOCONTROL_H
#define SERVOCONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "tim.h"
#include <stdint.h>
#include <stdbool.h>

// Configuración de servos
#define SERVO_COUNT                 4

// Pines de los servos
#define SERVO1_PIN                  GPIO_PIN_8      // PB8
#define SERVO1_GPIO_PORT            GPIOB
#define SERVO2_PIN                  GPIO_PIN_3      // PA3
#define SERVO2_GPIO_PORT            GPIOA
#define SERVO3_PIN                  GPIO_PIN_2      // PA2
#define SERVO3_GPIO_PORT            GPIOA
#define SERVO4_PIN                  GPIO_PIN_1      // PA1
#define SERVO4_GPIO_PORT            GPIOA

// Configuración PWM para servos (50Hz = 20ms período)
#define SERVO_PWM_FREQUENCY         50              // 50Hz
#define SERVO_PWM_PERIOD_MS         20              // 20ms

// Pulsos típicos para servos (en microsegundos)
#define SERVO_PULSE_MIN_US          1000            // 1ms = 0°
#define SERVO_PULSE_CENTER_US       1500            // 1.5ms = 90°
#define SERVO_PULSE_MAX_US          2000            // 2ms = 180°

// Ángulos límite
#define SERVO_ANGLE_MIN             0
#define SERVO_ANGLE_MAX             180
#define SERVO_ANGLE_CENTER          90

// Estructura para un servo individual
typedef struct {
    uint8_t id;                     // ID del servo (0-3)
    TIM_HandleTypeDef *htim;        // Timer PWM
    uint32_t channel;               // Canal del timer
    GPIO_TypeDef *gpio_port;        // Puerto GPIO
    uint16_t gpio_pin;              // Pin GPIO
    uint16_t current_angle;         // Ángulo actual
    uint16_t target_angle;          // Ángulo objetivo
    bool is_enabled;                // Servo habilitado
} Servo_t;

// Estructura principal del controlador de servos
typedef struct {
    Servo_t servos[SERVO_COUNT];
    bool is_initialized;
    uint16_t pwm_period;            // Período PWM calculado
} ServoControl_t;

// Funciones públicas
bool ServoControl_Init(ServoControl_t *ctrl);
bool ServoControl_SetTimers(ServoControl_t *ctrl, TIM_HandleTypeDef *htim4, TIM_HandleTypeDef *htim2);
bool ServoControl_SetAngle(ServoControl_t *ctrl, uint8_t servo_id, uint16_t angle);
bool ServoControl_SetAngleSmooth(ServoControl_t *ctrl, uint8_t servo_id, uint16_t angle, uint16_t speed_ms);
bool ServoControl_EnableServo(ServoControl_t *ctrl, uint8_t servo_id);
bool ServoControl_DisableServo(ServoControl_t *ctrl, uint8_t servo_id);
bool ServoControl_EnableAll(ServoControl_t *ctrl);
bool ServoControl_DisableAll(ServoControl_t *ctrl);

// Funciones de configuración
bool ServoControl_SetAllAngles(ServoControl_t *ctrl, uint16_t angle1, uint16_t angle2, uint16_t angle3, uint16_t angle4);
bool ServoControl_Center(ServoControl_t *ctrl, uint8_t servo_id);
bool ServoControl_CenterAll(ServoControl_t *ctrl);

// Funciones de consulta
uint16_t ServoControl_GetAngle(ServoControl_t *ctrl, uint8_t servo_id);
bool ServoControl_IsEnabled(ServoControl_t *ctrl, uint8_t servo_id);
bool ServoControl_IsValidAngle(uint16_t angle);

// Funciones de movimiento
bool ServoControl_Sweep(ServoControl_t *ctrl, uint8_t servo_id, uint16_t angle_min, uint16_t angle_max, uint16_t step_delay_ms);
bool ServoControl_SweepAll(ServoControl_t *ctrl, uint16_t step_delay_ms);

// Funciones de calibración
uint16_t ServoControl_AngleToPulse(uint16_t angle);
uint16_t ServoControl_PulseToAngle(uint16_t pulse_us);

// Macros de conveniencia
#define SERVO_1                     0
#define SERVO_2                     1
#define SERVO_3                     2
#define SERVO_4                     3

#ifdef __cplusplus
}
#endif

#endif // SERVOCONTROL_H