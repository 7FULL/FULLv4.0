#ifndef ROCKET_STATE_MACHINE_H
#define ROCKET_STATE_MACHINE_H

#include "main.h"
#include "KX134.h"
#include "MS5611.h"
#include "ZOE_M8Q.h"
#include "WS2812B.h"
#include "Buzzer.h"
#include "SPIFlash.h"
#include "PyroChannels.h"

typedef struct {
    float launch_detection_threshold;    // G threshold for launch detection
    float coast_detection_threshold;     // G threshold for coast detection
    uint32_t apogee_descent_time_ms;     // Time in coast to confirm apogee
    float altitude_stable_threshold;     // Altitude difference for stable detection
    uint32_t stable_time_landing_ms;     // Time stable to confirm landing
    uint32_t sleep_timeout_ms;           // Time in sleep before arming
    uint32_t data_logging_frequency_ms;  // Frequency of data logging
    bool simulation_mode_enabled;        // Enable/disable simulation mode
} RocketConfig_t;

typedef enum {
    ROCKET_STATE_SLEEP = 0,
    ROCKET_STATE_ARMED,
    ROCKET_STATE_BOOST,
    ROCKET_STATE_COAST,
    ROCKET_STATE_APOGEE,
    ROCKET_STATE_PARACHUTE,
    ROCKET_STATE_LANDED
} RocketState_t;

typedef struct {
    float acceleration_x;
    float acceleration_y;
    float acceleration_z;
    float angular_velocity_x;
    float angular_velocity_y;
    float angular_velocity_z;
    float pressure;
    float temperature;
    float altitude;
    float latitude;
    float longitude;
    float gps_altitude;
    uint32_t timestamp;
    RocketState_t rocket_state;
} FlightData_t;

typedef struct {
    RocketState_t current_state;
    RocketState_t previous_state;
    uint32_t state_start_time;

    FlightData_t current_data;
    RocketConfig_t config;

    float ground_altitude;
    float max_altitude;
    float apogee_altitude;
    float last_altitude;
    uint32_t stable_altitude_start_time;

    bool pyro_channel1_active;
    uint32_t pyro_channel1_start_time;

    bool sensors_initialized;
    bool data_logging_active;
    bool simulation_mode;

    uint32_t total_data_points;
    uint32_t spi_write_address;

    KX134_t* accelerometer;
    MS5611_t* barometer;
    ZOE_M8Q_t* gps;
    WS2812B_t* status_led;
    Buzzer_t* buzzer;
    SPIFlash_t* spi_flash;

} RocketStateMachine_t;

bool RocketStateMachine_Init(RocketStateMachine_t* rocket,
                           KX134_t* accel,
                           MS5611_t* baro,
                           ZOE_M8Q_t* gps,
                           WS2812B_t* led,
                           Buzzer_t* buzzer,
                           SPIFlash_t* flash);

void RocketStateMachine_Update(RocketStateMachine_t* rocket);
void RocketStateMachine_ChangeState(RocketStateMachine_t* rocket, RocketState_t new_state);
const char* RocketStateMachine_GetStateName(RocketState_t state);
bool RocketStateMachine_ReadSensors(RocketStateMachine_t* rocket);
bool RocketStateMachine_LogData(RocketStateMachine_t* rocket);
void RocketStateMachine_UpdateLED(RocketStateMachine_t* rocket);
void RocketStateMachine_UpdateBuzzer(RocketStateMachine_t* rocket);
bool RocketStateMachine_TransferDataToSD(RocketStateMachine_t* rocket);
bool RocketStateMachine_CheckAndRecoverFlashData(RocketStateMachine_t* rocket);
bool RocketStateMachine_CheckAndRecoverFlashData_EarlyInit(SPIFlash_t* spiflash);
bool RocketStateMachine_IsFlashEmpty(RocketStateMachine_t* rocket);
bool RocketStateMachine_EraseFlashData(RocketStateMachine_t* rocket);
uint32_t RocketStateMachine_CountDataPoints(RocketStateMachine_t* rocket);
bool RocketStateMachine_LoadConfig(RocketStateMachine_t* rocket);
void RocketStateMachine_LoadDefaultConfig(RocketStateMachine_t* rocket);

#endif // ROCKET_STATE_MACHINE_H