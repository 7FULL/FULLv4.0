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
    // Launch and flight detection
    float launch_detection_threshold;    // G threshold for launch detection
    float coast_detection_threshold;     // G threshold for coast detection
    uint32_t boost_timeout_ms;           // Maximum time in BOOST state (safety)
    uint32_t coast_timeout_ms;           // Maximum time in COAST state before apogee
    float altitude_stable_threshold;     // Altitude difference for stable detection
    uint32_t stable_time_landing_ms;     // Time stable to confirm landing
    uint32_t sleep_timeout_ms;           // Time in sleep before arming
    uint32_t data_logging_frequency_ms;  // Frequency of data logging
    bool simulation_mode_enabled;        // Enable/disable simulation mode

    // Sensor configuration
    uint8_t accelerometer_range;         // Accelerometer range (0=±8g, 1=±16g, 2=±32g, 3=±64g)
    uint8_t barometer_osr;               // MS5611 OSR index (0=OSR256 … 4=OSR4096). Higher = more
                                         // accurate but slower conversion. Conversion time is
                                         // derived automatically via MS5611_GetConversionTime_ms().

    // Flash pre-initialisation
    uint32_t flash_preinit_duration_s;   // Maximum expected flight duration from ARMED to LANDED (s).
                                         // Used to pre-erase exactly the needed flash sectors during
                                         // the ARMED state so no erase happens mid-flight.

    // Sensor timeouts (safety)
    uint32_t sensor_timeout_ms;          // Max time without valid sensor read (default: 1000ms)

    // Arming interlock requirements
    bool require_gps_lock;               // Require GPS lock before arming
    float arming_altitude_max_delta;     // Max altitude change during arming (default: 5m)
    uint32_t arming_stable_time_ms;      // Time altitude must be stable (default: 3000ms)

    // Multi-channel pyro configuration
    bool pyro_enable;                    // Global enable/disable for all pyro channels (default: true)
    uint8_t pyro_drogue_channel;         // Channel for drogue chute (0-3, default: 0)
    uint8_t pyro_main_channel;           // Channel for main chute (0-3, default: 1)
    uint8_t pyro_separation_channel;     // Channel for stage separation (0-3, default: 2)
    uint8_t pyro_backup_channel;         // Channel for backup (0-3, default: 3)
    uint32_t pyro_drogue_duration_ms;    // Drogue firing duration (default: 3000ms)
    uint32_t pyro_main_duration_ms;      // Main firing duration (default: 3000ms)
    float main_deploy_altitude_agl;      // Main chute deploy altitude AGL (default: 300m)

    // Apogee detection
    float apogee_altitude_drop_threshold; // Altitude drop from max to detect apogee (default: 5.0m)

    // Backup parachute deployment (safety)
    uint32_t backup_activation_delay_ms;  // Time to wait after main deployment before checking (default: 5000ms)
} RocketConfig_t;

typedef enum {
    ROCKET_STATE_SLEEP = 0,
    ROCKET_STATE_ARMED,
    ROCKET_STATE_BOOST,
    ROCKET_STATE_COAST,
    ROCKET_STATE_APOGEE,
    ROCKET_STATE_PARACHUTE,
    ROCKET_STATE_LANDED,
    ROCKET_STATE_ERROR,     // Sensor failure or critical error
    ROCKET_STATE_ABORT      // Mission abort - deploy recovery immediately
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
    uint8_t pyro_channel_states;  // Bit field: bit 0-3 for channels 0-3 (0=inactive, 1=active)
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

    // Multi-channel pyro tracking
    bool pyro_channels_active[4];        // Active state for each channel
    uint32_t pyro_channels_start_time[4]; // Activation time for each channel

    // Sensor health tracking
    uint32_t last_accel_update;          // Timestamp of last valid accelerometer read
    uint32_t last_baro_update;           // Timestamp of last valid barometer read
    uint32_t last_gps_update;            // Timestamp of last valid GPS read
    bool accel_valid;                    // Accelerometer health status
    bool baro_valid;                     // Barometer health status
    bool gps_valid;                      // GPS health status

    // Arming interlock state
    bool arming_conditions_met;          // All arming conditions satisfied
    uint32_t arming_stable_start_time;   // When altitude became stable
    float arming_reference_altitude;     // Altitude when arming started

    // Backup parachute deployment tracking
    bool main_chute_deployed;            // Main chute was deployed
    uint32_t main_chute_deploy_time;     // When main chute was activated
    bool backup_chute_activated;         // Backup chute was activated (prevent multiple activations)

    bool sensors_initialized;
    bool data_logging_active;
    bool simulation_mode;

    uint32_t total_data_points;
    uint32_t spi_write_address;
    uint32_t last_log_time;              // Last time data was logged (for frequency control)

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
bool RocketStateMachine_TransferDataToSD_Recovery(RocketStateMachine_t* rocket, const char* filename);
bool RocketStateMachine_CheckAndRecoverFlashData(RocketStateMachine_t* rocket);
bool RocketStateMachine_CheckAndRecoverFlashData_EarlyInit(SPIFlash_t* spiflash);
bool RocketStateMachine_IsFlashEmpty(RocketStateMachine_t* rocket);
bool RocketStateMachine_EraseFlashData(RocketStateMachine_t* rocket);
uint32_t RocketStateMachine_CountDataPoints(RocketStateMachine_t* rocket);
bool RocketStateMachine_LoadConfig(RocketStateMachine_t* rocket);
void RocketStateMachine_LoadDefaultConfig(RocketStateMachine_t* rocket);
void RocketStateMachine_SimulateFlightData(RocketStateMachine_t* rocket);

#endif // ROCKET_STATE_MACHINE_H