#include "ServoControl.h"
#include <string.h>

// Función auxiliar para convertir ángulo a valor PWM
static uint16_t AngleToPWMValue(uint16_t angle, uint16_t pwm_period) {
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;

    // Convertir ángulo (0-180°) a pulso (1000-2000µs)
    uint16_t pulse_us = SERVO_PULSE_MIN_US +
                       ((uint32_t)(angle * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US))) / SERVO_ANGLE_MAX;

    // Convertir pulso a valor PWM según el período del timer
    // Asumiendo que pwm_period corresponde a 20ms
    return (uint32_t)(pulse_us * pwm_period) / 20000;
}

// Configurar timer y canal para un servo
static bool ConfigureServoTimer(Servo_t *servo) {
    if (!servo || !servo->htim) return false;

    // Iniciar PWM en el canal correspondiente
    if (HAL_TIM_PWM_Start(servo->htim, servo->channel) != HAL_OK) {
        return false;
    }

    return true;
}

bool ServoControl_Init(ServoControl_t *ctrl) {
    if (!ctrl) return false;

    memset(ctrl, 0, sizeof(ServoControl_t));

    // Configurar servos individuales
    // SERVO 1 - PB8 (necesita timer configurado en CubeMX)
    ctrl->servos[0].id = 0;
    ctrl->servos[0].gpio_port = SERVO1_GPIO_PORT;
    ctrl->servos[0].gpio_pin = SERVO1_PIN;
    ctrl->servos[0].current_angle = SERVO_ANGLE_CENTER;
    ctrl->servos[0].target_angle = SERVO_ANGLE_CENTER;
    ctrl->servos[0].is_enabled = false;

    // SERVO 2 - PA3
    ctrl->servos[1].id = 1;
    ctrl->servos[1].gpio_port = SERVO2_GPIO_PORT;
    ctrl->servos[1].gpio_pin = SERVO2_PIN;
    ctrl->servos[1].current_angle = SERVO_ANGLE_CENTER;
    ctrl->servos[1].target_angle = SERVO_ANGLE_CENTER;
    ctrl->servos[1].is_enabled = false;

    // SERVO 3 - PA2
    ctrl->servos[2].id = 2;
    ctrl->servos[2].gpio_port = SERVO3_GPIO_PORT;
    ctrl->servos[2].gpio_pin = SERVO3_PIN;
    ctrl->servos[2].current_angle = SERVO_ANGLE_CENTER;
    ctrl->servos[2].target_angle = SERVO_ANGLE_CENTER;
    ctrl->servos[2].is_enabled = false;

    // SERVO 4 - PA1
    ctrl->servos[3].id = 3;
    ctrl->servos[3].gpio_port = SERVO4_GPIO_PORT;
    ctrl->servos[3].gpio_pin = SERVO4_PIN;
    ctrl->servos[3].current_angle = SERVO_ANGLE_CENTER;
    ctrl->servos[3].target_angle = SERVO_ANGLE_CENTER;
    ctrl->servos[3].is_enabled = false;

    // NOTA: Los timers deben configurarse en CubeMX para cada pin:
    // PB8 podría ser TIM4_CH3, PA3 podría ser TIM2_CH4, etc.
    // El período debe ser para 50Hz (20ms)

    ctrl->pwm_period = 999; // Valor por defecto, se ajustará según timer
    ctrl->is_initialized = true;

    return true;
}

bool ServoControl_SetTimers(ServoControl_t *ctrl, TIM_HandleTypeDef *htim4, TIM_HandleTypeDef *htim2) {
    if (!ctrl || !ctrl->is_initialized) return false;

    // Asignar timers y canales según tu configuración:
    // PB8 = TIM4_CH3, PA3 = TIM2_CH4, PA2 = TIM2_CH3, PA1 = TIM2_CH2

    ctrl->servos[0].htim = htim4;          // PB8 = TIM4_CH3
    ctrl->servos[0].channel = TIM_CHANNEL_3;

    ctrl->servos[1].htim = htim2;          // PA3 = TIM2_CH4
    ctrl->servos[1].channel = TIM_CHANNEL_4;

    ctrl->servos[2].htim = htim2;          // PA2 = TIM2_CH3
    ctrl->servos[2].channel = TIM_CHANNEL_3;

    ctrl->servos[3].htim = htim2;          // PA1 = TIM2_CH2
    ctrl->servos[3].channel = TIM_CHANNEL_2;

    // Obtener período del timer (asumiendo que todos son iguales para 50Hz)
    if (htim2) {
        ctrl->pwm_period = htim2->Init.Period;
    }

    return true;
}

bool ServoControl_SetAngle(ServoControl_t *ctrl, uint8_t servo_id, uint16_t angle) {
    if (!ctrl || !ctrl->is_initialized || servo_id >= SERVO_COUNT) return false;
    if (!ServoControl_IsValidAngle(angle)) return false;

    Servo_t *servo = &ctrl->servos[servo_id];
    if (!servo->is_enabled || !servo->htim) return false;

    // Calcular valor PWM
    uint16_t pwm_value = AngleToPWMValue(angle, ctrl->pwm_period);

    // Establecer duty cycle
    __HAL_TIM_SET_COMPARE(servo->htim, servo->channel, pwm_value);

    servo->current_angle = angle;
    servo->target_angle = angle;

    return true;
}

bool ServoControl_SetAngleSmooth(ServoControl_t *ctrl, uint8_t servo_id, uint16_t angle, uint16_t speed_ms) {
    if (!ctrl || !ctrl->is_initialized || servo_id >= SERVO_COUNT) return false;
    if (!ServoControl_IsValidAngle(angle)) return false;

    Servo_t *servo = &ctrl->servos[servo_id];
    if (!servo->is_enabled) return false;

    uint16_t current = servo->current_angle;
    uint16_t target = angle;

    if (current == target) return true;

    // Movimiento gradual
    int16_t step = (current < target) ? 1 : -1;
    uint16_t steps = (current < target) ? (target - current) : (current - target);
    uint16_t delay_per_step = speed_ms / steps;

    if (delay_per_step == 0) delay_per_step = 1;

    while (current != target) {
        current += step;
        ServoControl_SetAngle(ctrl, servo_id, current);
        HAL_Delay(delay_per_step);
    }

    return true;
}

bool ServoControl_EnableServo(ServoControl_t *ctrl, uint8_t servo_id) {
    if (!ctrl || !ctrl->is_initialized || servo_id >= SERVO_COUNT) return false;

    Servo_t *servo = &ctrl->servos[servo_id];

    if (!servo->htim) return false; // Timer no configurado

    // Configurar timer y canal
    if (!ConfigureServoTimer(servo)) return false;

    servo->is_enabled = true;

    // Establecer posición central por defecto
    ServoControl_SetAngle(ctrl, servo_id, SERVO_ANGLE_CENTER);

    return true;
}

bool ServoControl_DisableServo(ServoControl_t *ctrl, uint8_t servo_id) {
    if (!ctrl || !ctrl->is_initialized || servo_id >= SERVO_COUNT) return false;

    Servo_t *servo = &ctrl->servos[servo_id];

    if (servo->htim) {
        HAL_TIM_PWM_Stop(servo->htim, servo->channel);
    }

    servo->is_enabled = false;

    return true;
}

bool ServoControl_EnableAll(ServoControl_t *ctrl) {
    if (!ctrl || !ctrl->is_initialized) return false;

    bool success = true;
    for (uint8_t i = 0; i < SERVO_COUNT; i++) {
        if (!ServoControl_EnableServo(ctrl, i)) {
            success = false;
        }
    }

    return success;
}

bool ServoControl_DisableAll(ServoControl_t *ctrl) {
    if (!ctrl || !ctrl->is_initialized) return false;

    for (uint8_t i = 0; i < SERVO_COUNT; i++) {
        ServoControl_DisableServo(ctrl, i);
    }

    return true;
}

bool ServoControl_SetAllAngles(ServoControl_t *ctrl, uint16_t angle1, uint16_t angle2, uint16_t angle3, uint16_t angle4) {
    if (!ctrl || !ctrl->is_initialized) return false;

    bool success = true;
    success &= ServoControl_SetAngle(ctrl, 0, angle1);
    success &= ServoControl_SetAngle(ctrl, 1, angle2);
    success &= ServoControl_SetAngle(ctrl, 2, angle3);
    success &= ServoControl_SetAngle(ctrl, 3, angle4);

    return success;
}

bool ServoControl_Center(ServoControl_t *ctrl, uint8_t servo_id) {
    return ServoControl_SetAngle(ctrl, servo_id, SERVO_ANGLE_CENTER);
}

bool ServoControl_CenterAll(ServoControl_t *ctrl) {
    return ServoControl_SetAllAngles(ctrl, SERVO_ANGLE_CENTER, SERVO_ANGLE_CENTER,
                                     SERVO_ANGLE_CENTER, SERVO_ANGLE_CENTER);
}

uint16_t ServoControl_GetAngle(ServoControl_t *ctrl, uint8_t servo_id) {
    if (!ctrl || !ctrl->is_initialized || servo_id >= SERVO_COUNT) return 0;

    return ctrl->servos[servo_id].current_angle;
}

bool ServoControl_IsEnabled(ServoControl_t *ctrl, uint8_t servo_id) {
    if (!ctrl || !ctrl->is_initialized || servo_id >= SERVO_COUNT) return false;

    return ctrl->servos[servo_id].is_enabled;
}

bool ServoControl_IsValidAngle(uint16_t angle) {
    return (angle >= SERVO_ANGLE_MIN && angle <= SERVO_ANGLE_MAX);
}

bool ServoControl_Sweep(ServoControl_t *ctrl, uint8_t servo_id, uint16_t angle_min, uint16_t angle_max, uint16_t step_delay_ms) {
    if (!ctrl || !ctrl->is_initialized || servo_id >= SERVO_COUNT) return false;
    if (!ServoControl_IsValidAngle(angle_min) || !ServoControl_IsValidAngle(angle_max)) return false;
    if (angle_min >= angle_max) return false;

    // Sweep hacia adelante
    for (uint16_t angle = angle_min; angle <= angle_max; angle += 5) {
        ServoControl_SetAngle(ctrl, servo_id, angle);
        HAL_Delay(step_delay_ms);
    }

    // Sweep hacia atrás
    for (uint16_t angle = angle_max; angle >= angle_min && angle <= angle_max; angle -= 5) {
        ServoControl_SetAngle(ctrl, servo_id, angle);
        HAL_Delay(step_delay_ms);
    }

    return true;
}

bool ServoControl_SweepAll(ServoControl_t *ctrl, uint16_t step_delay_ms) {
    if (!ctrl || !ctrl->is_initialized) return false;

    bool success = true;
    for (uint8_t i = 0; i < SERVO_COUNT; i++) {
        if (ctrl->servos[i].is_enabled) {
            success &= ServoControl_Sweep(ctrl, i, SERVO_ANGLE_MIN, SERVO_ANGLE_MAX, step_delay_ms);
        }
    }

    return success;
}

uint16_t ServoControl_AngleToPulse(uint16_t angle) {
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;

    return SERVO_PULSE_MIN_US +
           ((uint32_t)(angle * (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US))) / SERVO_ANGLE_MAX;
}

uint16_t ServoControl_PulseToAngle(uint16_t pulse_us) {
    if (pulse_us < SERVO_PULSE_MIN_US) pulse_us = SERVO_PULSE_MIN_US;
    if (pulse_us > SERVO_PULSE_MAX_US) pulse_us = SERVO_PULSE_MAX_US;

    return ((uint32_t)(pulse_us - SERVO_PULSE_MIN_US) * SERVO_ANGLE_MAX) /
           (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US);
}