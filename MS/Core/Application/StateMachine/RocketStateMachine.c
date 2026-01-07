#include "RocketStateMachine.h"
#include "SDLogger.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

// Valores por defecto - serán sobrescritos por configuración de SD
#define DEFAULT_LAUNCH_DETECTION_THRESHOLD 2.5f    // 2.5G acceleration threshold
#define DEFAULT_COAST_DETECTION_THRESHOLD  1.5f    // 1.5G coast detection
#define DEFAULT_BOOST_TIMEOUT_MS          10000    // 10 seconds max in BOOST (safety for stuck motor)
#define DEFAULT_COAST_TIMEOUT_MS           5000    // 5 seconds max in COAST before forcing apogee
#define DEFAULT_ALTITUDE_STABLE_THRESHOLD  2.0f    // 2m difference to consider stable
#define DEFAULT_STABLE_TIME_LANDING_MS     8000    // 8 seconds stable altitude to confirm landing
#define DEFAULT_SLEEP_TIMEOUT_MS          10000    // 10 seconds in sleep before arming
#define DEFAULT_DATA_LOGGING_FREQ_MS         5     // 5ms = 200Hz logging frequency
#define DEFAULT_SIMULATION_MODE_ENABLED   false   // Simulation mode disabled by default

// Sensor configuration defaults
#define DEFAULT_ACCELEROMETER_RANGE          2     // ±32g range (0=±8g, 1=±16g, 2=±32g, 3=±64g)

// Safety defaults
#define DEFAULT_SENSOR_TIMEOUT_MS          1000    // 1 second sensor timeout
#define DEFAULT_REQUIRE_GPS_LOCK          false   // GPS not required by default
#define DEFAULT_ARMING_ALTITUDE_MAX_DELTA   5.0f   // 5m max altitude change during arming
#define DEFAULT_ARMING_STABLE_TIME_MS      3000    // 3 seconds stable before arming

// Multi-pyro defaults
#define DEFAULT_PYRO_ENABLE               true     // Pyro channels enabled by default
#define DEFAULT_PYRO_DROGUE_CHANNEL          0     // Channel 0 for drogue
#define DEFAULT_PYRO_MAIN_CHANNEL            1     // Channel 1 for main
#define DEFAULT_PYRO_SEPARATION_CHANNEL      2     // Channel 2 for separation
#define DEFAULT_PYRO_BACKUP_CHANNEL          3     // Channel 3 for backup
#define DEFAULT_PYRO_DROGUE_DURATION_MS   3000     // 3 seconds
#define DEFAULT_PYRO_MAIN_DURATION_MS     3000     // 3 seconds
#define DEFAULT_MAIN_DEPLOY_ALTITUDE_AGL 300.0f    // 300m AGL for main chute

// Improved apogee detection
#define DEFAULT_APOGEE_VELOCITY_THRESHOLD       2.0f    // 2.0 m/s vertical velocity
#define DEFAULT_APOGEE_ALTITUDE_DROP_THRESHOLD  5.0f    // 5.0m altitude drop from max

// Backup parachute deployment (safety)
#define DEFAULT_BACKUP_ACTIVATION_DELAY_MS     5000     // 5 seconds after main deployment
#define DEFAULT_BACKUP_VELOCITY_THRESHOLD    -10.0f     // -10 m/s (descending faster than 10 m/s)

extern SDLogger_t sdlogger;

static const char* state_names[] = {
    "SLEEP",
    "ARMED",
    "BOOST",
    "COAST",
    "APOGEE",
    "PARACHUTE",
    "LANDED",
    "ERROR",
    "ABORT"
};

bool RocketStateMachine_Init(RocketStateMachine_t* rocket,
                           KX134_t* accel,
                           MS5611_t* baro,
                           ZOE_M8Q_t* gps,
                           WS2812B_t* led,
                           Buzzer_t* buzzer,
                           SPIFlash_t* flash) {

    if (!rocket || !accel || !baro || !led || !buzzer || !flash) {
        return false;
    }

    memset(rocket, 0, sizeof(RocketStateMachine_t));

    rocket->accelerometer = accel;
    rocket->barometer = baro;
    rocket->gps = gps;
    rocket->status_led = led;
    rocket->buzzer = buzzer;
    rocket->spi_flash = flash;

    rocket->current_state = ROCKET_STATE_SLEEP;
    rocket->previous_state = ROCKET_STATE_SLEEP;
    rocket->state_start_time = HAL_GetTick();

    rocket->sensors_initialized = false;
    rocket->data_logging_active = false;
    rocket->simulation_mode = false;
    rocket->total_data_points = 0;
    rocket->spi_write_address = 0x000000;
    rocket->last_log_time = 0;

    // Inicializar multi-pyro channels
    for (uint8_t i = 0; i < 4; i++) {
        rocket->pyro_channels_active[i] = false;
        rocket->pyro_channels_start_time[i] = 0;
    }

    // Inicializar sensor health tracking
    uint32_t now = HAL_GetTick();
    rocket->last_accel_update = now;
    rocket->last_baro_update = now;
    rocket->last_gps_update = now;
    rocket->accel_valid = true;
    rocket->baro_valid = true;
    rocket->gps_valid = false;  // GPS starts invalid until first fix

    // Inicializar backup parachute tracking
    rocket->main_chute_deployed = false;
    rocket->main_chute_deploy_time = 0;
    rocket->backup_chute_activated = false;

    // Cargar configuración desde SD PRIMERO
    RocketStateMachine_LoadConfig(rocket);

    // Aplicar configuración de simulación
    rocket->simulation_mode = rocket->config.simulation_mode_enabled;

    // LED and Buzzer already initialized in main.c

    // Inicializar hardware solo si NO estamos en modo simulación
    if (!rocket->simulation_mode) {
        SDLogger_WriteText(&sdlogger, "Initializing sensors for REAL FLIGHT mode...");

        // Initialize accelerometer on SPI1, CS=PB1
        if (!KX134_Init(rocket->accelerometer, &hspi1, GPIOB, GPIO_PIN_1)) {
            SDLogger_WriteText(&sdlogger, "ERROR: KX134 initialization failed");
            return false;
        }

        // Configure accelerometer range from config file
        if (!KX134_Configure(rocket->accelerometer, rocket->config.accelerometer_range)) {
            SDLogger_WriteText(&sdlogger, "ERROR: KX134 configuration failed");
            return false;
        }

        // Enable accelerometer
        if (!KX134_Enable(rocket->accelerometer)) {
            SDLogger_WriteText(&sdlogger, "ERROR: KX134 enable failed");
            return false;
        }

        const char* range_names[] = {"±8g", "±16g", "±32g", "±64g"};
        char accel_msg[80];
        sprintf(accel_msg, "KX134 accelerometer OK (Range: %s)", range_names[rocket->config.accelerometer_range]);
        SDLogger_WriteText(&sdlogger, accel_msg);

        // Initialize barometer on SPI1, CS=PC4
        if (!MS5611_Init(rocket->barometer, &hspi1, GPIOC, GPIO_PIN_4)) {
            SDLogger_WriteText(&sdlogger, "ERROR: MS5611 initialization failed");
            return false;
        }
        SDLogger_WriteText(&sdlogger, "MS5611 barometer OK");

        // Initialize GPS on I2C3 (optional - can fail)
        if (!ZOE_M8Q_Init(rocket->gps, &hi2c3)) {
            SDLogger_WriteText(&sdlogger, "WARNING: GPS initialization failed (optional)");
        } else {
            SDLogger_WriteText(&sdlogger, "ZOE-M8Q GPS OK");
        }

        // Initialize SPI Flash on SPI1
        if (!SPIFlash_Init(rocket->spi_flash, &hspi1)) {
            SDLogger_WriteText(&sdlogger, "ERROR: SPIFlash initialization failed");
            return false;
        }
        SDLogger_WriteText(&sdlogger, "W25Q128 Flash OK");

        rocket->sensors_initialized = true;
    } else {
        // En modo simulación, solo inicializar Flash (LED/Buzzer ya inicializados)
        SDLogger_WriteText(&sdlogger, "");
        SDLogger_WriteText(&sdlogger, "=== SIMULATION MODE ENABLED ===");
        SDLogger_WriteText(&sdlogger, "Skipping real sensor initialization");

        if (!SPIFlash_Init(rocket->spi_flash, &hspi1)) {
            SDLogger_WriteText(&sdlogger, "ERROR: SPIFlash initialization failed");
            return false;
        }
        SDLogger_WriteText(&sdlogger, "W25Q128 Flash OK");

        rocket->sensors_initialized = true;
    }

    // Leer sensores iniciales o simular
    if (!RocketStateMachine_ReadSensors(rocket)) {
        if (!rocket->simulation_mode) {
            return false;  // Solo falla si no estamos en simulación
        }
    }

    rocket->ground_altitude = rocket->current_data.altitude;
    rocket->max_altitude = rocket->ground_altitude;
    rocket->apogee_altitude = rocket->ground_altitude;
    rocket->last_altitude = rocket->current_data.altitude;
    rocket->stable_altitude_start_time = HAL_GetTick();

    // Inicializar arming interlock
    rocket->arming_conditions_met = false;
    rocket->arming_stable_start_time = now;
    rocket->arming_reference_altitude = rocket->ground_altitude;

    // Inicializar velocity tracking
    rocket->vertical_velocity = 0.0f;
    rocket->last_velocity_altitude = rocket->ground_altitude;
    rocket->last_velocity_time = now;

    char init_msg[100];
    sprintf(init_msg, "ROCKET: Initialized at altitude: %ld.%02dm",
           (int32_t)(rocket->ground_altitude),
           (int32_t)(rocket->ground_altitude * 100) % 100);
    SDLogger_WriteText(&sdlogger, init_msg);

    // Verificar si hay datos de vuelos anteriores en Flash (recuperación de emergencia)
    SDLogger_WriteText(&sdlogger, "");
    SDLogger_WriteText(&sdlogger, "=== CHECKING FOR PREVIOUS FLIGHT DATA ===");
    if (!RocketStateMachine_IsFlashEmpty(rocket)) {
        SDLogger_WriteText(&sdlogger, "WARNING: Flash contains data from previous flight!");
        SDLogger_WriteText(&sdlogger, "Initiating emergency data recovery...");

        // LED naranja durante recuperación
        WS2812B_SetColorRGB(rocket->status_led, 255, 165, 0);
		HAL_Delay(500);

        if (RocketStateMachine_CheckAndRecoverFlashData(rocket)) {
            SDLogger_WriteText(&sdlogger, "SUCCESS: Previous flight data recovered to SD card");
            SDLogger_WriteText(&sdlogger, "Flash memory has been erased and is ready for new flight");
        } else {
            SDLogger_WriteText(&sdlogger, "ERROR: Failed to recover previous flight data");
            SDLogger_WriteText(&sdlogger, "WARNING: Flash may still contain old data");
			return false;
        }
    } else {
        SDLogger_WriteText(&sdlogger, "Flash is empty - ready for new flight");
    }

    return true;
}

void RocketStateMachine_SimulateFlightData(RocketStateMachine_t* rocket) {
    if (!rocket) return;

    static uint32_t sim_start_time = 0;
    static bool sim_initialized = false;

    // Initialize simulation ONLY when ARMED (ready to launch)
    if (!sim_initialized && rocket->current_state == ROCKET_STATE_ARMED) {
        sim_start_time = HAL_GetTick();
        sim_initialized = true;
        SDLogger_WriteText(&sdlogger, "=== SIMULATION FLIGHT STARTED ===");
    }

    // If not initialized yet (still in SLEEP), just provide ground data
    if (!sim_initialized) {
        rocket->current_data.acceleration_x = 1.0f;  // 1G gravity
        rocket->current_data.acceleration_y = 0.0f;
        rocket->current_data.acceleration_z = 0.0f;

        // Use fixed ground altitude for initial readings (Madrid: ~667m MSL)
        float initial_altitude = (rocket->ground_altitude > 0) ? rocket->ground_altitude : 667.0f;
        rocket->current_data.altitude = initial_altitude;
        rocket->current_data.pressure = 101325.0f - (initial_altitude * 12.0f);  // Barometric formula
        rocket->current_data.temperature = 20.0f;
        rocket->current_data.latitude = 40.4168f;
        rocket->current_data.longitude = -3.7038f;
        rocket->current_data.gps_altitude = initial_altitude;
        rocket->accel_valid = true;
        rocket->baro_valid = true;
        return;
    }

    // Calculate elapsed time in seconds FROM ARMED STATE
    float elapsed_sec = (HAL_GetTick() - sim_start_time) / 1000.0f;

    // Simulated flight profile (starts immediately from ARMED)
    if (elapsed_sec < 2.0f) {
        // Phase 0: On pad waiting for launch (0-2s)
        rocket->current_data.acceleration_x = 1.0f;  // 1G gravity
        rocket->current_data.altitude = rocket->ground_altitude;

    } else if (elapsed_sec < 5.0f) {
        // Phase 1: Boost (2-5s) - 6G average
        float boost_time = elapsed_sec - 2.0f;
        rocket->current_data.acceleration_x = 6.0f + sinf(boost_time * 3.14f) * 2.0f;  // 4-8G varying
        rocket->current_data.altitude = rocket->ground_altitude + (boost_time * boost_time * 40.0f);  // ~360m at burnout

    } else if (elapsed_sec < 10.0f) {
        // Phase 2: Coast (5-10s) - decelerating
        float coast_time = elapsed_sec - 5.0f;
        rocket->current_data.acceleration_x = 1.0f - (coast_time * 0.15f);  // Decreasing to ~0.25G

        // Altitude peaks around 8-9 seconds at ~600m
        float coast_velocity = 120.0f - (coast_time * 24.0f);  // Deceleration
        rocket->current_data.altitude = 360.0f + rocket->ground_altitude + (coast_velocity * coast_time) - (12.0f * coast_time * coast_time);

    } else if (elapsed_sec < 25.0f) {
        // Phase 3: Drogue descent (10-25s)
        float descent_time = elapsed_sec - 10.0f;
        rocket->current_data.acceleration_x = 1.0f;  // ~1G

        // Descend from apogee (~600m) at ~8 m/s with drogue
        float apogee = 600.0f + rocket->ground_altitude;
        rocket->current_data.altitude = apogee - (descent_time * 8.0f);

    } else if (elapsed_sec < 40.0f) {
        // Phase 4: Main descent (25-40s)
        float main_time = elapsed_sec - 25.0f;
        rocket->current_data.acceleration_x = 1.0f;

        // Descend from ~480m at ~5 m/s with main
        rocket->current_data.altitude = (480.0f + rocket->ground_altitude) - (main_time * 5.0f);

        // Ensure we don't go below ground
        if (rocket->current_data.altitude < rocket->ground_altitude) {
            rocket->current_data.altitude = rocket->ground_altitude;
        }

    } else {
        // Phase 5: Landed (40s+)
        rocket->current_data.acceleration_x = 1.0f;
        rocket->current_data.altitude = rocket->ground_altitude;
    }

    // Simulated other sensors
    rocket->current_data.acceleration_y = 0.0f;
    rocket->current_data.acceleration_z = 0.0f;
    rocket->current_data.pressure = 101325.0f - (rocket->current_data.altitude - rocket->ground_altitude) * 12.0f;  // ~12 Pa/m
    rocket->current_data.temperature = 20.0f - (rocket->current_data.altitude - rocket->ground_altitude) * 0.0065f;  // Lapse rate

    // Simulated GPS (slowly drifting position)
    rocket->current_data.latitude = 40.4168f + (elapsed_sec * 0.0001f);
    rocket->current_data.longitude = -3.7038f + (elapsed_sec * 0.0001f);
    rocket->current_data.gps_altitude = rocket->current_data.altitude;

    // Sensor health always valid in simulation
    rocket->accel_valid = true;
    rocket->baro_valid = true;
    rocket->gps_valid = true;

    // Log simulation progress periodically
    static uint32_t last_log = 0;
    if (HAL_GetTick() - last_log > 5000) {
        char sim_msg[100];
        sprintf(sim_msg, "SIM: t=%.1fs alt=%.1fm accel=%.1fG state=%s",
                elapsed_sec,
                rocket->current_data.altitude - rocket->ground_altitude,
                rocket->current_data.acceleration_x,
                state_names[rocket->current_state]);
        SDLogger_WriteText(&sdlogger, sim_msg);
        last_log = HAL_GetTick();
    }
}

void RocketStateMachine_Update(RocketStateMachine_t* rocket) {
    if (!rocket || !rocket->sensors_initialized) {
        return;
    }

    // Simulate flight data if in simulation mode
    if (rocket->simulation_mode) {
        RocketStateMachine_SimulateFlightData(rocket);
    }

    // Read sensors and check for critical failures
    if (!RocketStateMachine_ReadSensors(rocket)) {
        // Critical sensor failure - enter ERROR state
        if (rocket->current_state != ROCKET_STATE_ERROR &&
            rocket->current_state != ROCKET_STATE_ABORT) {
            RocketStateMachine_ChangeState(rocket, ROCKET_STATE_ERROR);
            SDLogger_WriteText(&sdlogger, "ERROR: Critical sensor failure detected");
        }
        return;
    }

    RocketState_t next_state = rocket->current_state;
    uint32_t time_in_state = HAL_GetTick() - rocket->state_start_time;
    uint32_t now = HAL_GetTick();

    switch (rocket->current_state) {
        case ROCKET_STATE_SLEEP:
            // Check arming interlock conditions
            if (time_in_state > rocket->config.sleep_timeout_ms) {
                // Check altitude stability
                float altitude_delta = fabsf(rocket->current_data.altitude - rocket->arming_reference_altitude);

                // Diagnostic logging (only every 2 seconds)
                static uint32_t last_diagnostic = 0;
                if (now - last_diagnostic > 2000) {
                    char diag[150];
                    sprintf(diag, "SLEEP: timeout OK, alt_delta=%.1fm (max=%.1f), stable=%lums (need=%lu), gps_ok=%d",
                           altitude_delta, rocket->config.arming_altitude_max_delta,
                           now - rocket->arming_stable_start_time, rocket->config.arming_stable_time_ms,
                           !rocket->config.require_gps_lock || rocket->gps_valid);
                    SDLogger_WriteText(&sdlogger, diag);
                    last_diagnostic = now;
                }

                if (altitude_delta < rocket->config.arming_altitude_max_delta) {
                    uint32_t stable_duration = now - rocket->arming_stable_start_time;
                    if (stable_duration >= rocket->config.arming_stable_time_ms) {
                        // Check GPS lock if required
                        bool gps_ok = !rocket->config.require_gps_lock || rocket->gps_valid;
                        if (gps_ok) {
                            rocket->arming_conditions_met = true;
                            SDLogger_WriteText(&sdlogger, "ARMING CONDITIONS MET - Transitioning to ARMED");
                            next_state = ROCKET_STATE_ARMED;
                        } else {
                            SDLogger_WriteText(&sdlogger, "SLEEP: Waiting for GPS lock");
                        }
                    }
                } else {
                    // Altitude changed, reset stability timer
                    rocket->arming_stable_start_time = now;
                    rocket->arming_reference_altitude = rocket->current_data.altitude;

                    char reset_msg[100];
                    sprintf(reset_msg, "SLEEP: Altitude changed by %.1fm, resetting stability timer", altitude_delta);
                    SDLogger_WriteText(&sdlogger, reset_msg);
                }
            } else {
                // Still in initial timeout period
                static uint32_t last_timeout_log = 0;
                if (now - last_timeout_log > 1000) {
                    char timeout_msg[100];
                    sprintf(timeout_msg, "SLEEP: Waiting for timeout (%lu/%lu ms)",
                           time_in_state, rocket->config.sleep_timeout_ms);
                    SDLogger_WriteText(&sdlogger, timeout_msg);
                    last_timeout_log = now;
                }
            }
            break;

        case ROCKET_STATE_ARMED:
            if (rocket->current_data.acceleration_x > rocket->config.launch_detection_threshold) {
                next_state = ROCKET_STATE_BOOST;
                SDLogger_WriteText(&sdlogger, "LAUNCH DETECTED");
            }
            break;

        case ROCKET_STATE_BOOST:
            // Normal transition: motor burnout detected
            if (rocket->current_data.acceleration_x < rocket->config.coast_detection_threshold) {
                next_state = ROCKET_STATE_COAST;
            }

            // Safety timeout: motor burning too long (stuck igniter, etc.)
            if (time_in_state > rocket->config.boost_timeout_ms) {
                SDLogger_WriteText(&sdlogger, "WARNING: BOOST timeout - forcing APOGEE");
                next_state = ROCKET_STATE_COAST;
            }
            break;

        case ROCKET_STATE_COAST:
            // Track max altitude
            if (rocket->current_data.altitude > rocket->max_altitude) {
                rocket->max_altitude = rocket->current_data.altitude;
            }

            // Apogee detection - multiple independent methods
            bool apogee_detected = false;

            // Method 1: Velocity-based (PRIMARY - most accurate)
            if (rocket->vertical_velocity < -rocket->config.apogee_velocity_threshold) {
                apogee_detected = true;  // Descending at significant rate
                SDLogger_WriteText(&sdlogger, "APOGEE: Detected by velocity");
            }

            // Method 2: Altitude drop (FALLBACK - if velocity calculation fails)
            if (rocket->current_data.altitude < (rocket->max_altitude - rocket->config.apogee_altitude_drop_threshold)) {
                apogee_detected = true;  // Altitude dropped significantly
                SDLogger_WriteText(&sdlogger, "APOGEE: Detected by altitude drop");
            }

            // Method 3: Time-based safety (EMERGENCY - something went wrong)
            if (time_in_state > rocket->config.coast_timeout_ms) {
                apogee_detected = true;  // Been coasting too long
                SDLogger_WriteText(&sdlogger, "APOGEE: Detected by timeout (safety)");
            }

            if (apogee_detected) {
                rocket->apogee_altitude = rocket->max_altitude;
                next_state = ROCKET_STATE_APOGEE;
            }
            break;

        case ROCKET_STATE_APOGEE:
            // Deploy drogue immediately in RocketStateMachine_ChangeState
            next_state = ROCKET_STATE_PARACHUTE;
            break;

        case ROCKET_STATE_PARACHUTE:
            // Check for main chute deployment altitude
            float altitude_agl = rocket->current_data.altitude - rocket->ground_altitude;
            if (altitude_agl <= rocket->config.main_deploy_altitude_agl && altitude_agl > 0) {
                // Activate main chute channel if not already active
                uint8_t main_ch = rocket->config.pyro_main_channel;
                if (!rocket->pyro_channels_active[main_ch]) {
                    rocket->pyro_channels_active[main_ch] = true;
                    rocket->pyro_channels_start_time[main_ch] = now;

                    // Only activate if pyro channels are enabled
                    if (rocket->config.pyro_enable) {
                        PyroChannels_ActivateChannel(main_ch);
                        SDLogger_WriteText(&sdlogger, "MAIN CHUTE DEPLOYED");
                    } else {
                        SDLogger_WriteText(&sdlogger, "MAIN CHUTE DEPLOYMENT SKIPPED (PYRO_DISABLED)");
                    }

                    // Track main chute deployment for backup activation check
                    rocket->main_chute_deployed = true;
                    rocket->main_chute_deploy_time = now;
                }
            }

            // BACKUP PARACHUTE SAFETY: Check if main chute failed to deploy properly
            // If descending too fast after configured delay, activate backup channel
            if (rocket->main_chute_deployed && !rocket->backup_chute_activated) {
                uint32_t time_since_main = now - rocket->main_chute_deploy_time;

                // Wait configured delay before checking (allow main chute to deploy and slow descent)
                if (time_since_main >= rocket->config.backup_activation_delay_ms) {
                    // Check if vertical velocity indicates main chute failure
                    // If still descending faster than threshold, activate backup
                    if (rocket->vertical_velocity < rocket->config.backup_velocity_threshold) {
                        uint8_t backup_ch = rocket->config.pyro_backup_channel;

                        // Activate backup channel
                        rocket->pyro_channels_active[backup_ch] = true;
                        rocket->pyro_channels_start_time[backup_ch] = now;
                        rocket->backup_chute_activated = true;

                        // Only activate if pyro channels are enabled
                        if (rocket->config.pyro_enable) {
                            PyroChannels_ActivateChannel(backup_ch);

                            // Log the backup activation with velocity data
                            char backup_msg[120];
                            sprintf(backup_msg, "BACKUP CHUTE ACTIVATED - Main failed (velocity: %.2f m/s)",
                                    rocket->vertical_velocity);
                            SDLogger_WriteText(&sdlogger, backup_msg);
                        } else {
                            char backup_msg[120];
                            sprintf(backup_msg, "BACKUP CHUTE SKIPPED (PYRO_DISABLED) - velocity: %.2f m/s",
                                    rocket->vertical_velocity);
                            SDLogger_WriteText(&sdlogger, backup_msg);
                        }
                    }
                }
            }

            // Check for landing
            if (fabsf(rocket->current_data.altitude - rocket->last_altitude) < rocket->config.altitude_stable_threshold) {
                uint32_t stable_time = now - rocket->stable_altitude_start_time;
                if (stable_time > rocket->config.stable_time_landing_ms) {
                    next_state = ROCKET_STATE_LANDED;
                }
            } else {
                rocket->last_altitude = rocket->current_data.altitude;
                rocket->stable_altitude_start_time = now;
            }
            break;

        case ROCKET_STATE_LANDED:
            if (!rocket->data_logging_active) {
                static bool transfer_completed = false;
                if (!transfer_completed) {
                    transfer_completed = RocketStateMachine_TransferDataToSD(rocket);
                    if (transfer_completed) {
                        RocketStateMachine_EraseFlashData(rocket);
                        SDLogger_WriteText(&sdlogger, "Flight complete - Flash erased");
                    }
                }
            }
            break;

        case ROCKET_STATE_ERROR:
            // Stay in ERROR state, log continuously
            SDLogger_WriteText(&sdlogger, "ERROR state - sensor recovery attempt");
            break;

        case ROCKET_STATE_ABORT:
            // Deploy all recovery immediately
            for (uint8_t ch = 0; ch < 4; ch++) {
                if (!rocket->pyro_channels_active[ch]) {
                    rocket->pyro_channels_active[ch] = true;
                    rocket->pyro_channels_start_time[ch] = now;

                    // Only activate if pyro channels are enabled
                    if (rocket->config.pyro_enable) {
                        PyroChannels_ActivateChannel(ch);
                    }
                }
            }

            if (rocket->config.pyro_enable) {
                SDLogger_WriteText(&sdlogger, "ABORT - All recovery deployed");
            } else {
                SDLogger_WriteText(&sdlogger, "ABORT - Recovery deployment skipped (PYRO_DISABLED)");
            }
            break;
    }

    // State transition
    if (next_state != rocket->current_state) {
        RocketStateMachine_ChangeState(rocket, next_state);
    }

    // Data logging (con control de frecuencia)
    if (rocket->data_logging_active && rocket->current_state != ROCKET_STATE_LANDED) {
        uint32_t current_time = HAL_GetTick();
        if ((current_time - rocket->last_log_time) >= rocket->config.data_logging_frequency_ms) {
            RocketStateMachine_LogData(rocket);
            rocket->last_log_time = current_time;
        }
    }

    // Multi-channel pyro management
    for (uint8_t ch = 0; ch < 4; ch++) {
        if (rocket->pyro_channels_active[ch]) {
            uint32_t duration_ms = (ch == rocket->config.pyro_drogue_channel) ?
                                   rocket->config.pyro_drogue_duration_ms :
                                   rocket->config.pyro_main_duration_ms;

            uint32_t elapsed = now - rocket->pyro_channels_start_time[ch];
            if (elapsed >= duration_ms) {
                rocket->pyro_channels_active[ch] = false;
                PyroChannels_DeactivateChannel(ch);
            }
        }
    }

    RocketStateMachine_UpdateLED(rocket);
    RocketStateMachine_UpdateBuzzer(rocket);
}

void RocketStateMachine_ChangeState(RocketStateMachine_t* rocket, RocketState_t new_state) {
    if (!rocket || new_state == rocket->current_state) {
        return;
    }

    uint32_t now = HAL_GetTick();

    // Log state transitions
    char state_msg[100];
    sprintf(state_msg, "STATE: %s -> %s",
           state_names[rocket->current_state],
           state_names[new_state]);
    SDLogger_WriteText(&sdlogger, state_msg);

    // Handle state-specific actions
    if (new_state == ROCKET_STATE_ARMED) {
        // Start data logging
        rocket->data_logging_active = true;
        SDLogger_WriteText(&sdlogger, "Data logging STARTED");
    }

    if (new_state == ROCKET_STATE_APOGEE) {
        // Deploy drogue chute at apogee
        uint8_t drogue_ch = rocket->config.pyro_drogue_channel;
        rocket->pyro_channels_active[drogue_ch] = true;
        rocket->pyro_channels_start_time[drogue_ch] = now;

        // Only activate if pyro channels are enabled
        if (rocket->config.pyro_enable) {
            PyroChannels_ActivateChannel(drogue_ch);
            SDLogger_WriteText(&sdlogger, "DROGUE DEPLOYED");
        } else {
            SDLogger_WriteText(&sdlogger, "DROGUE DEPLOYMENT SKIPPED (PYRO_DISABLED)");
        }
    }

    if (new_state == ROCKET_STATE_ERROR) {
        // Log sensor states
        char error_msg[150];
        sprintf(error_msg, "ERROR: Accel=%d Baro=%d GPS=%d",
                rocket->accel_valid, rocket->baro_valid, rocket->gps_valid);
        SDLogger_WriteText(&sdlogger, error_msg);
    }

    if (new_state == ROCKET_STATE_ABORT) {
        SDLogger_WriteText(&sdlogger, "ABORT STATE ENTERED");
    }

    if (new_state == ROCKET_STATE_LANDED) {
        rocket->data_logging_active = false;
        char landing_msg[100];
        sprintf(landing_msg, "LANDED: Max alt=%ld.%02dm, Points=%ld",
               (int32_t)(rocket->max_altitude),
               (int32_t)(rocket->max_altitude * 100) % 100,
               rocket->total_data_points);
        SDLogger_WriteText(&sdlogger, landing_msg);
    }

    rocket->previous_state = rocket->current_state;
    rocket->current_state = new_state;
    rocket->state_start_time = now;
}

const char* RocketStateMachine_GetStateName(RocketState_t state) {
    if (state >= 0 && state < (sizeof(state_names) / sizeof(state_names[0]))) {
        return state_names[state];
    }
    return "UNKNOWN";
}

bool RocketStateMachine_ReadSensors(RocketStateMachine_t* rocket) {
    if (!rocket) return false;

    uint32_t now = HAL_GetTick();
    rocket->current_data.timestamp = now;

    // In simulation mode, use simulated data and skip real sensor reads
    if (rocket->simulation_mode) {
        RocketStateMachine_SimulateFlightData(rocket);

        // Set sensor health for simulation (always valid)
        rocket->accel_valid = true;
        rocket->baro_valid = true;
        // GPS validity handled by simulation function

        // Include current rocket state in data
        rocket->current_data.rocket_state = rocket->current_state;

        // Capture pyro channel states
        rocket->current_data.pyro_channel_states = 0;
        for (uint8_t ch = 0; ch < 4; ch++) {
            if (PyroChannels_IsChannelActive(ch)) {
                rocket->current_data.pyro_channel_states |= (1 << ch);
            }
        }

        return true;  // Simulation always succeeds
    }

    // === REAL SENSOR READING (only when NOT in simulation) ===

    // Read accelerometer and track health
    if (!rocket->simulation_mode) {
        KX134_AccelData_t accel_data;
        if (KX134_ReadAccelG(rocket->accelerometer, &accel_data)) {
            rocket->current_data.acceleration_x = accel_data.x;
            rocket->current_data.acceleration_y = accel_data.y;
            rocket->current_data.acceleration_z = accel_data.z;
            rocket->last_accel_update = now;
            rocket->accel_valid = true;
        } else {
            // Check for timeout
            if ((now - rocket->last_accel_update) > rocket->config.sensor_timeout_ms) {
                rocket->accel_valid = false;
            }
        }
    }

    // KX134 has no gyro, set angular velocities to 0
    rocket->current_data.angular_velocity_x = 0.0f;
    rocket->current_data.angular_velocity_y = 0.0f;
    rocket->current_data.angular_velocity_z = 0.0f;

    // Read barometer and track health
    MS5611_Data_t ms_data;
    if (MS5611_ReadData(rocket->barometer, &ms_data)) {
        rocket->current_data.pressure = ms_data.pressure;
        rocket->current_data.temperature = ms_data.temperature;

        if (!rocket->simulation_mode) {
            rocket->current_data.altitude = (float)ms_data.altitude;
        }
        rocket->last_baro_update = now;
        rocket->baro_valid = true;
    } else {
        // Check for timeout
        if ((now - rocket->last_baro_update) > rocket->config.sensor_timeout_ms) {
            rocket->baro_valid = false;
        }
    }

    // Read GPS at lower frequency (GPS only updates ~1Hz, no need to poll every cycle)
    // Poll GPS only every 200ms to avoid blocking I2C
    static uint32_t last_gps_read = 0;
    if (rocket->gps && (now - last_gps_read) >= 200) {
        last_gps_read = now;

        ZOE_M8Q_ReadData(rocket->gps);
        if (ZOE_M8Q_HasValidFix(rocket->gps)) {
            rocket->current_data.latitude = rocket->gps->gps_data.latitude;
            rocket->current_data.longitude = rocket->gps->gps_data.longitude;
            rocket->current_data.gps_altitude = rocket->gps->gps_data.altitude;
            rocket->last_gps_update = now;
            rocket->gps_valid = true;
        } else {
            // GPS timeout only matters if GPS is required
            if (rocket->config.require_gps_lock) {
                if ((now - rocket->last_gps_update) > rocket->config.sensor_timeout_ms) {
                    rocket->gps_valid = false;
                }
            }
        }
    }

    // Calculate vertical velocity for improved apogee detection
    uint32_t dt = now - rocket->last_velocity_time;
    if (dt >= 5) {  // Update velocity every 5ms (200Hz) - ultra-high-speed apogee detection
        float altitude_delta = rocket->current_data.altitude - rocket->last_velocity_altitude;
        float dt_seconds = dt / 1000.0f;
        rocket->vertical_velocity = altitude_delta / dt_seconds;  // m/s
        rocket->last_velocity_altitude = rocket->current_data.altitude;
        rocket->last_velocity_time = now;
    }

    // Copy vertical velocity to flight data
    rocket->current_data.vertical_velocity = rocket->vertical_velocity;

    // Include current rocket state in data
    rocket->current_data.rocket_state = rocket->current_state;

    // Capture pyro channel states (bit 0-3 for channels 0-3)
    rocket->current_data.pyro_channel_states = 0;
    for (uint8_t ch = 0; ch < 4; ch++) {
        if (PyroChannels_IsChannelActive(ch)) {
            rocket->current_data.pyro_channel_states |= (1 << ch);
        }
    }

    // Check critical sensor health
    if (!rocket->accel_valid || !rocket->baro_valid) {
        //return false;  // Critical sensor failure (right now we comment this because we are not manually deploying)
    }

    return true;
}

bool RocketStateMachine_LogData(RocketStateMachine_t* rocket) {
    if (!rocket || !rocket->data_logging_active) {
        return false;
    }

    uint8_t data_buffer[64];
    memcpy(data_buffer, &rocket->current_data, sizeof(FlightData_t));

    if (SPIFlash_WriteData(rocket->spi_flash, rocket->spi_write_address, data_buffer, sizeof(FlightData_t))) {
        rocket->spi_write_address += sizeof(FlightData_t);
        rocket->total_data_points++;
        return true;
    }

    return false;
}

void RocketStateMachine_UpdateLED(RocketStateMachine_t* rocket) {
    if (!rocket || !rocket->status_led) return;

    static uint32_t last_blink = 0;
    static bool blink_state = false;
    uint32_t now = HAL_GetTick();

    switch (rocket->current_state) {
        case ROCKET_STATE_SLEEP:
            WS2812B_SetColorRGB(rocket->status_led, 128, 0, 128); // Purple
            break;
        case ROCKET_STATE_ARMED:
            WS2812B_SetColorRGB(rocket->status_led, 255, 255, 0); // Yellow
            break;
        case ROCKET_STATE_BOOST:
            WS2812B_SetColorRGB(rocket->status_led, 255, 0, 0);   // Red
            break;
        case ROCKET_STATE_COAST:
            WS2812B_SetColorRGB(rocket->status_led, 0, 0, 255);   // Blue
            break;
        case ROCKET_STATE_APOGEE:
            WS2812B_SetColorRGB(rocket->status_led, 255, 255, 255); // White
            break;
        case ROCKET_STATE_PARACHUTE:
            WS2812B_SetColorRGB(rocket->status_led, 0, 255, 255); // Cyan
            break;
        case ROCKET_STATE_LANDED:
            WS2812B_SetColorRGB(rocket->status_led, 0, 255, 0);   // Green
            break;
        case ROCKET_STATE_ERROR:
            // Blink red rapidly
            if (now - last_blink > 250) {
                blink_state = !blink_state;
                last_blink = now;
            }
            if (blink_state) {
                WS2812B_SetColorRGB(rocket->status_led, 255, 0, 0);  // Red
            } else {
                WS2812B_SetColorRGB(rocket->status_led, 0, 0, 0);    // Off
            }
            break;
        case ROCKET_STATE_ABORT:
            // Blink orange rapidly
            if (now - last_blink > 150) {
                blink_state = !blink_state;
                last_blink = now;
            }
            if (blink_state) {
                WS2812B_SetColorRGB(rocket->status_led, 255, 165, 0); // Orange
            } else {
                WS2812B_SetColorRGB(rocket->status_led, 0, 0, 0);     // Off
            }
            break;
        default:
            WS2812B_SetColorRGB(rocket->status_led, 255, 255, 255); // White
            break;
    }
}

void RocketStateMachine_UpdateBuzzer(RocketStateMachine_t* rocket) {
    if (!rocket || !rocket->buzzer) return;

    static uint32_t last_buzz_time = 0;
    uint32_t current_time = HAL_GetTick();

    switch (rocket->current_state) {
        case ROCKET_STATE_ARMED:
            if (current_time - last_buzz_time > 2000) {
                Buzzer_Pattern(rocket->buzzer, BUZZER_PATTERN_INIT);
                last_buzz_time = current_time;
            }
            break;

        case ROCKET_STATE_BOOST:
            if (current_time - last_buzz_time > 500) {
                Buzzer_Pattern(rocket->buzzer, BUZZER_PATTERN_SUCCESS);
                last_buzz_time = current_time;
            }
            break;

        case ROCKET_STATE_APOGEE:
            Buzzer_Pattern(rocket->buzzer, BUZZER_PATTERN_SUCCESS);
            break;

        case ROCKET_STATE_LANDED:
            if (current_time - last_buzz_time > 3000) {
                Buzzer_Pattern(rocket->buzzer, BUZZER_PATTERN_SUCCESS);
                last_buzz_time = current_time;
            }
            break;

        case ROCKET_STATE_ERROR:
            // Continuous error tone
            if (current_time - last_buzz_time > 500) {
                BUZZER_ERROR(rocket->buzzer);
                last_buzz_time = current_time;
            }
            break;

        case ROCKET_STATE_ABORT:
            // Rapid error pattern
            if (current_time - last_buzz_time > 300) {
                BUZZER_ERROR(rocket->buzzer);
                last_buzz_time = current_time;
            }
            break;

        default:
            break;
    }
}

bool RocketStateMachine_TransferDataToSD(RocketStateMachine_t* rocket) {
    if (!rocket || rocket->total_data_points == 0) {
        return false;
    }

    // Solo escribir a SD si está disponible
    if (!sdlogger.is_mounted) {
        return false; // Sin SD, no se puede transferir pero no es error crítico
    }

    char filename[80];

    // Verificar si la carpeta flights/ existe
    FILINFO fno;
    FRESULT dir_check = f_stat("flights", &fno);

    int flight_number;
    if (dir_check == FR_OK && (fno.fattrib & AM_DIR)) {
        // La carpeta flights/ existe, usarla
        flight_number = SDLogger_GetNextFlightFileName(filename, sizeof(filename), "flight_data", "flights");
    } else {
        // La carpeta no existe, guardar en raíz
        flight_number = SDLogger_GetNextFlightFileName(filename, sizeof(filename), "flight_data", "");
        SDLogger_WriteText(&sdlogger, "logs/flights_folder_not_found.txt");
    }

    if (flight_number < 0) {
        SDLogger_WriteText(&sdlogger, "logs/flight_filename_error.txt");
        return false;
    }

    // Abrir archivo CSV directamente para escribir línea por línea
    FIL csv_file;
    FRESULT result = f_open(&csv_file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        char error_msg[100];
        sprintf(error_msg, "logs/csv_file_error_%d.txt", (int)result);
        SDLogger_WriteText(&sdlogger, error_msg);
        return false;
    }

    UINT bytes_written;

    // Escribir header
    char header[] = "Timestamp,AccelX,AccelY,AccelZ,GyroX,GyroY,GyroZ,Pressure,Temperature,Altitude,VerticalVelocity,Latitude,Longitude,GPS_Alt,State,Pyro0,Pyro1,Pyro2,Pyro3\r\n";
    result = f_write(&csv_file, header, strlen(header), &bytes_written);
    if (result != FR_OK) {
        f_close(&csv_file);
        return false;
    }

    // Leer datos del Flash y escribir línea por línea
    uint32_t read_address = 0x000000;
    uint8_t data_buffer[64];
    FlightData_t flight_data;

    for (uint32_t i = 0; i < rocket->total_data_points; i++) {
        if (SPIFlash_ReadData(rocket->spi_flash, read_address, data_buffer, sizeof(FlightData_t))) {
            memcpy(&flight_data, data_buffer, sizeof(FlightData_t));

            // Extraer estados de los canales pirotécnicos (bit 0-3)
            uint8_t pyro0 = (flight_data.pyro_channel_states & 0x01) ? 1 : 0;
            uint8_t pyro1 = (flight_data.pyro_channel_states & 0x02) ? 1 : 0;
            uint8_t pyro2 = (flight_data.pyro_channel_states & 0x04) ? 1 : 0;
            uint8_t pyro3 = (flight_data.pyro_channel_states & 0x08) ? 1 : 0;

            char csv_line[300];
            sprintf(csv_line, "%ld,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%02d,%ld.%02d,%ld.%02d,%ld.%03d,%ld.%06d,%ld.%06d,%ld.%02d,%s,%d,%d,%d,%d\r\n",
                   flight_data.timestamp,
                   (int32_t)(flight_data.acceleration_x), abs((int32_t)(flight_data.acceleration_x * 1000) % 1000),
                   (int32_t)(flight_data.acceleration_y), abs((int32_t)(flight_data.acceleration_y * 1000) % 1000),
                   (int32_t)(flight_data.acceleration_z), abs((int32_t)(flight_data.acceleration_z * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_x), abs((int32_t)(flight_data.angular_velocity_x * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_y), abs((int32_t)(flight_data.angular_velocity_y * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_z), abs((int32_t)(flight_data.angular_velocity_z * 1000) % 1000),
                   (int32_t)(flight_data.pressure), abs((int32_t)(flight_data.pressure * 100) % 100),
                   (int32_t)(flight_data.temperature), abs((int32_t)(flight_data.temperature * 100) % 100),
                   (int32_t)(flight_data.altitude), abs((int32_t)(flight_data.altitude * 100) % 100),
                   (int32_t)(flight_data.vertical_velocity), abs((int32_t)(flight_data.vertical_velocity * 1000) % 1000),
                   (int32_t)(flight_data.latitude), abs((int32_t)(flight_data.latitude * 1000000) % 1000000),
                   (int32_t)(flight_data.longitude), abs((int32_t)(flight_data.longitude * 1000000) % 1000000),
                   (int32_t)(flight_data.gps_altitude), abs((int32_t)(flight_data.gps_altitude * 100) % 100),
                   state_names[flight_data.rocket_state],
                   pyro0, pyro1, pyro2, pyro3);

            // Escribir línea directamente al archivo
            result = f_write(&csv_file, csv_line, strlen(csv_line), &bytes_written);
            if (result != FR_OK) {
                f_close(&csv_file);
                return false;
            }

            read_address += sizeof(FlightData_t);
        } else {
            f_close(&csv_file);
            return false;
        }
    }

    // Cerrar archivo
    f_close(&csv_file);
    bool success = true;

    if (success) {
        char completion_msg[150];
        sprintf(completion_msg, "CSV file created: %s with %ld data points", filename, rocket->total_data_points);
        SDLogger_WriteText(&sdlogger, completion_msg);
    }

    return success;
}

bool RocketStateMachine_TransferDataToSD_Recovery(RocketStateMachine_t* rocket, const char* filename) {
    if (!rocket || !filename) return false;

    // Solo escribir a SD si está disponible
    if (!sdlogger.is_mounted) {
        return false;
    }

    // Abrir archivo CSV directamente para escribir línea por línea
    FIL csv_file;
    FRESULT result = f_open(&csv_file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        char error_msg[100];
        sprintf(error_msg, "ERROR: No se pudo abrir %s (FRESULT=%d)", filename, (int)result);
        SDLogger_WriteText(&sdlogger, error_msg);
        return false;
    }

    UINT bytes_written;

    // Escribir header
    char header[] = "Timestamp,AccelX,AccelY,AccelZ,GyroX,GyroY,GyroZ,Pressure,Temperature,Altitude,VerticalVelocity,Latitude,Longitude,GPS_Alt,State,Pyro0,Pyro1,Pyro2,Pyro3\r\n";
    result = f_write(&csv_file, header, strlen(header), &bytes_written);
    if (result != FR_OK) {
        f_close(&csv_file);
        return false;
    }

    // Leer datos del Flash y escribir línea por línea
    uint32_t read_address = 0x000000;
    uint8_t data_buffer[64];
    FlightData_t flight_data;

    for (uint32_t i = 0; i < rocket->total_data_points; i++) {
        if (SPIFlash_ReadData(rocket->spi_flash, read_address, data_buffer, sizeof(FlightData_t))) {
            memcpy(&flight_data, data_buffer, sizeof(FlightData_t));

            // Extraer estados de los canales pirotécnicos (bit 0-3)
            uint8_t pyro0 = (flight_data.pyro_channel_states & 0x01) ? 1 : 0;
            uint8_t pyro1 = (flight_data.pyro_channel_states & 0x02) ? 1 : 0;
            uint8_t pyro2 = (flight_data.pyro_channel_states & 0x04) ? 1 : 0;
            uint8_t pyro3 = (flight_data.pyro_channel_states & 0x08) ? 1 : 0;

            char csv_line[300];
            sprintf(csv_line, "%ld,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%02d,%ld.%02d,%ld.%02d,%ld.%03d,%ld.%06d,%ld.%06d,%ld.%02d,%s,%d,%d,%d,%d\r\n",
                   flight_data.timestamp,
                   (int32_t)(flight_data.acceleration_x), abs((int32_t)(flight_data.acceleration_x * 1000) % 1000),
                   (int32_t)(flight_data.acceleration_y), abs((int32_t)(flight_data.acceleration_y * 1000) % 1000),
                   (int32_t)(flight_data.acceleration_z), abs((int32_t)(flight_data.acceleration_z * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_x), abs((int32_t)(flight_data.angular_velocity_x * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_y), abs((int32_t)(flight_data.angular_velocity_y * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_z), abs((int32_t)(flight_data.angular_velocity_z * 1000) % 1000),
                   (int32_t)(flight_data.pressure), abs((int32_t)(flight_data.pressure * 100) % 100),
                   (int32_t)(flight_data.temperature), abs((int32_t)(flight_data.temperature * 100) % 100),
                   (int32_t)(flight_data.altitude), abs((int32_t)(flight_data.altitude * 100) % 100),
                   (int32_t)(flight_data.vertical_velocity), abs((int32_t)(flight_data.vertical_velocity * 1000) % 1000),
                   (int32_t)(flight_data.latitude), abs((int32_t)(flight_data.latitude * 1000000) % 1000000),
                   (int32_t)(flight_data.longitude), abs((int32_t)(flight_data.longitude * 1000000) % 1000000),
                   (int32_t)(flight_data.gps_altitude), abs((int32_t)(flight_data.gps_altitude * 100) % 100),
                   state_names[flight_data.rocket_state],
                   pyro0, pyro1, pyro2, pyro3);

            // Escribir línea directamente al archivo
            result = f_write(&csv_file, csv_line, strlen(csv_line), &bytes_written);
            if (result != FR_OK) {
                f_close(&csv_file);
                return false;
            }

            read_address += sizeof(FlightData_t);
        } else {
            f_close(&csv_file);
            return false;
        }
    }

    // Cerrar archivo
    f_close(&csv_file);

    char completion_msg[150];
    sprintf(completion_msg, "Recovery file created: %s with %ld data points", filename, rocket->total_data_points);
    SDLogger_WriteText(&sdlogger, completion_msg);

    return true;
}

bool RocketStateMachine_CheckAndRecoverFlashData_EarlyInit(SPIFlash_t* spiflash) {
    if (!spiflash) return false;

    // Verificar si Flash está vacío
    uint8_t test_buffer[256];
    bool is_empty = true;

    for (uint32_t addr = 0; addr < 4096; addr += 256) {
        if (SPIFlash_ReadData(spiflash, addr, test_buffer, 256)) {
            for (int i = 0; i < 256; i++) {
                if (test_buffer[i] != 0xFF) {
                    is_empty = false;
                    break;
                }
            }
            if (!is_empty) break;
        }
    }

    if (is_empty) {
        SDLogger_WriteText(&sdlogger, "Flash vacío - No hay datos previos que recuperar");
        return true;
    }

    // Contar datos válidos
    uint32_t count = 0;
    uint32_t addr = 0;
    uint8_t data_buffer[sizeof(FlightData_t)];
    FlightData_t flight_data;

    while (addr < SPIFlash_GetTotalSize(spiflash) && count < 1000) { // Límite de seguridad
        if (SPIFlash_ReadData(spiflash, addr, data_buffer, sizeof(FlightData_t))) {
            memcpy(&flight_data, data_buffer, sizeof(FlightData_t));

            if (flight_data.timestamp != 0 && flight_data.timestamp != 0xFFFFFFFF) {
                count++;
                addr += sizeof(FlightData_t);
            } else {
                break;
            }
        } else {
            break;
        }
    }

    if (count == 0) {
        SDLogger_WriteText(&sdlogger, "Flash contiene datos pero no se encontraron puntos válidos");
        return true;
    }

    char recovery_msg[100];
    sprintf(recovery_msg, "¡RECUPERACIÓN DETECTADA! Flash contiene %ld puntos de datos", count);
    SDLogger_WriteText(&sdlogger, recovery_msg);

    // Transferir datos usando función simplificada
    if (sdlogger.is_mounted) {
        char filename[80];

        // Verificar si la carpeta recovery_data/ existe
        FILINFO fno;
        FRESULT dir_check = f_stat("recovery_data", &fno);

        int recovery_number;
        if (dir_check == FR_OK && (fno.fattrib & AM_DIR)) {
            // La carpeta recovery_data/ existe, usarla
            recovery_number = SDLogger_GetNextFlightFileName(filename, sizeof(filename), "recovered_data", "recovery_data");
        } else {
            // La carpeta no existe, guardar en raíz
            recovery_number = SDLogger_GetNextFlightFileName(filename, sizeof(filename), "recovered_data", "");
        }

        if (recovery_number < 0) {
            SDLogger_WriteText(&sdlogger, "logs/recovery_filename_error.txt");
            return false;
        }

        char header[] = "Timestamp,AccelX,AccelY,AccelZ,GyroX,GyroY,GyroZ,Pressure,Temperature,Altitude,VerticalVelocity,Latitude,Longitude,GPS_Alt,Pyro0,Pyro1,Pyro2,Pyro3";

        char csv_data[20000];
        csv_data[0] = '\0';

        uint32_t read_address = 0x000000;
        for (uint32_t i = 0; i < count && i < 50; i++) { // Limitar a 50 puntos por simplicidad
            if (SPIFlash_ReadData(spiflash, read_address, data_buffer, sizeof(FlightData_t))) {
                memcpy(&flight_data, data_buffer, sizeof(FlightData_t));

                // Extraer estados de los canales pirotécnicos (bit 0-3)
                uint8_t pyro0 = (flight_data.pyro_channel_states & 0x01) ? 1 : 0;
                uint8_t pyro1 = (flight_data.pyro_channel_states & 0x02) ? 1 : 0;
                uint8_t pyro2 = (flight_data.pyro_channel_states & 0x04) ? 1 : 0;
                uint8_t pyro3 = (flight_data.pyro_channel_states & 0x08) ? 1 : 0;

                char csv_line[300];
                sprintf(csv_line, "%ld,%ld.%03d,%ld.%03d,%ld.%03d,0.000,0.000,0.000,%ld.%02d,%ld.%02d,%ld.%02d,0.000000,0.000000,0.00,%d,%d,%d,%d\r\n",
                       flight_data.timestamp,
                       (int32_t)(flight_data.acceleration_x), abs((int32_t)(flight_data.acceleration_x * 1000) % 1000),
                       (int32_t)(flight_data.acceleration_y), abs((int32_t)(flight_data.acceleration_y * 1000) % 1000),
                       (int32_t)(flight_data.acceleration_z), abs((int32_t)(flight_data.acceleration_z * 1000) % 1000),
                       (int32_t)(flight_data.pressure), abs((int32_t)(flight_data.pressure * 100) % 100),
                       (int32_t)(flight_data.temperature), abs((int32_t)(flight_data.temperature * 100) % 100),
                       (int32_t)(flight_data.altitude), abs((int32_t)(flight_data.altitude * 100) % 100),
                       pyro0, pyro1, pyro2, pyro3);

                strcat(csv_data, csv_line);
                read_address += sizeof(FlightData_t);
            }
        }

        if (SDLogger_WriteCSVFile(&sdlogger, filename, header, csv_data)) {
            char success_msg[100];
            sprintf(success_msg, "Datos recuperados exitosamente en %s", filename);
            SDLogger_WriteText(&sdlogger, success_msg);

            // Borrar Flash después de recuperación exitosa
            SDLogger_WriteText(&sdlogger, "Borrando Flash tras recuperación exitosa...");
            uint32_t sectors_to_erase = ((count * sizeof(FlightData_t)) / 4096) + 1;
            if (sectors_to_erase > 50) sectors_to_erase = 50;

            for (uint32_t i = 0; i < sectors_to_erase; i++) {
                SPIFlash_EraseSector(spiflash, i * 4096);
            }

            SDLogger_WriteText(&sdlogger, "Flash limpiado - Listo para nuevo vuelo");
        } else {
            SDLogger_WriteText(&sdlogger, "ERROR: No se pudo crear archivo de recuperación");
        }
    } else {
        SDLogger_WriteText(&sdlogger, "WARNING: Sin SD disponible, datos quedan en Flash");
    }

    return true;
}

bool RocketStateMachine_IsFlashEmpty(RocketStateMachine_t* rocket) {
    if (!rocket || !rocket->spi_flash) return true;

    uint8_t test_buffer[256];
    bool is_empty = true;

    // Verificar las primeras páginas para detectar datos
    for (uint32_t addr = 0; addr < 4096; addr += 256) { // Verificar primer sector
        if (SPIFlash_ReadData(rocket->spi_flash, addr, test_buffer, 256)) {
            for (int i = 0; i < 256; i++) {
                if (test_buffer[i] != 0xFF) { // 0xFF es el estado "borrado"
                    is_empty = false;
                    break;
                }
            }
            if (!is_empty) break;
        }
    }

    return is_empty;
}

uint32_t RocketStateMachine_CountDataPoints(RocketStateMachine_t* rocket) {
    if (!rocket || !rocket->spi_flash) return 0;

    uint32_t count = 0;
    uint32_t addr = 0;
    uint8_t data_buffer[sizeof(FlightData_t)];
    FlightData_t flight_data;

    while (addr < SPIFlash_GetTotalSize(rocket->spi_flash)) {
        if (SPIFlash_ReadData(rocket->spi_flash, addr, data_buffer, sizeof(FlightData_t))) {
            memcpy(&flight_data, data_buffer, sizeof(FlightData_t));

            // Verificar si los datos son válidos (timestamp != 0 y != 0xFFFFFFFF)
            if (flight_data.timestamp != 0 && flight_data.timestamp != 0xFFFFFFFF) {
                count++;
                addr += sizeof(FlightData_t);
            } else {
                break; // No más datos válidos
            }
        } else {
            break;
        }
    }

    return count;
}

bool RocketStateMachine_CheckAndRecoverFlashData(RocketStateMachine_t* rocket) {
    if (!rocket) return false;

    SDLogger_WriteText(&sdlogger, "=== VERIFICANDO DATOS PREVIOS EN FLASH ===");

    if (RocketStateMachine_IsFlashEmpty(rocket)) {
        SDLogger_WriteText(&sdlogger, "Flash vacío - No hay datos previos que recuperar");
        return true;
    }

    uint32_t data_points = RocketStateMachine_CountDataPoints(rocket);
    if (data_points == 0) {
        SDLogger_WriteText(&sdlogger, "Flash contiene datos pero no se encontraron puntos válidos");
        return true;
    }

    char recovery_msg[100];
    sprintf(recovery_msg, "¡RECUPERACIÓN DETECTADA! Flash contiene %ld puntos de datos", data_points);
    SDLogger_WriteText(&sdlogger, recovery_msg);

    // Señal visual de recuperación
    for (int i = 0; i < 5; i++) {
        WS2812B_SetColorRGB(rocket->status_led, 255, 165, 0); // Naranja = recuperación
        HAL_Delay(200);
        WS2812B_SetColorRGB(rocket->status_led, 0, 0, 0);
        HAL_Delay(200);
    }

    // Crear carpeta recovery/ si no existe
    FRESULT dir_result = f_mkdir("recovery");
    if (dir_result != FR_OK && dir_result != FR_EXIST) {
        SDLogger_WriteText(&sdlogger, "WARNING: No se pudo crear carpeta recovery/");
    }

    // Recuperar los datos a la carpeta recovery/
    rocket->total_data_points = data_points;

    // Generar nombre de archivo de recuperación con timestamp
    char filename[80];
    int recovery_number = SDLogger_GetNextFlightFileName(filename, sizeof(filename), "recovery", "recovery");

    if (recovery_number < 0) {
        SDLogger_WriteText(&sdlogger, "ERROR: No se pudo generar nombre de archivo de recuperación");
        return false;
    }

    // Transferir datos del Flash a SD usando el archivo de recuperación
    if (RocketStateMachine_TransferDataToSD_Recovery(rocket, filename)) {
        char success_msg[120];
        sprintf(success_msg, "Datos recuperados exitosamente a: %s", filename);
        SDLogger_WriteText(&sdlogger, success_msg);
        SDLogger_WriteText(&sdlogger, "Borrando Flash...");

        if (RocketStateMachine_EraseFlashData(rocket)) {
            SDLogger_WriteText(&sdlogger, "Flash borrado - Sistema listo para nuevo vuelo");
            WS2812B_SetColorRGB(rocket->status_led, 0, 255, 0); // Verde = éxito
            BUZZER_SUCCESS(rocket->buzzer);
            HAL_Delay(1000);
        } else {
            SDLogger_WriteText(&sdlogger, "ERROR: No se pudo borrar el Flash después de la recuperación");
            WS2812B_SetColorRGB(rocket->status_led, 255, 255, 0); // Amarillo = warning
        }
    } else {
        SDLogger_WriteText(&sdlogger, "ERROR: No se pudieron recuperar los datos a SD");
        WS2812B_SetColorRGB(rocket->status_led, 255, 0, 0); // Rojo = error
        BUZZER_ERROR(rocket->buzzer);
        return false;
    }

    // Reset del contador para el nuevo vuelo
    rocket->total_data_points = 0;
    rocket->spi_write_address = 0x000000;

    return true;
}

bool RocketStateMachine_EraseFlashData(RocketStateMachine_t* rocket) {
    if (!rocket || !rocket->spi_flash) return false;

    SDLogger_WriteText(&sdlogger, "Borrando sector de datos del Flash...");

    // Borrar los primeros sectores donde se guardan los datos
    uint32_t sectors_to_erase = ((rocket->total_data_points * sizeof(FlightData_t)) / 4096) + 1;
    if (sectors_to_erase > 100) sectors_to_erase = 100; // Máximo 100 sectores por seguridad

    for (uint32_t i = 0; i < sectors_to_erase; i++) {
        uint32_t sector_addr = i * 4096;
        if (!SPIFlash_EraseSector(rocket->spi_flash, sector_addr)) {
            char error_msg[50];
            sprintf(error_msg, "Error borrando sector %ld", i);
            SDLogger_WriteText(&sdlogger, error_msg);
            return false;
        }

        // Feedback visual del progreso
        if (i % 10 == 0) {
            WS2812B_SetColorRGB(rocket->status_led, 0, 0, 255); // Azul = borrando
            HAL_Delay(50);
            WS2812B_SetColorRGB(rocket->status_led, 0, 0, 0);
            HAL_Delay(50);
        }
    }

    char erase_msg[50];
    sprintf(erase_msg, "Flash borrado - %ld sectores limpiados", sectors_to_erase);
    SDLogger_WriteText(&sdlogger, erase_msg);

    return true;
}

void RocketStateMachine_LoadDefaultConfig(RocketStateMachine_t* rocket) {
    if (!rocket) return;

    // Flight detection
    rocket->config.launch_detection_threshold = DEFAULT_LAUNCH_DETECTION_THRESHOLD;
    rocket->config.coast_detection_threshold = DEFAULT_COAST_DETECTION_THRESHOLD;
    rocket->config.boost_timeout_ms = DEFAULT_BOOST_TIMEOUT_MS;
    rocket->config.coast_timeout_ms = DEFAULT_COAST_TIMEOUT_MS;
    rocket->config.altitude_stable_threshold = DEFAULT_ALTITUDE_STABLE_THRESHOLD;
    rocket->config.stable_time_landing_ms = DEFAULT_STABLE_TIME_LANDING_MS;
    rocket->config.sleep_timeout_ms = DEFAULT_SLEEP_TIMEOUT_MS;
    rocket->config.data_logging_frequency_ms = DEFAULT_DATA_LOGGING_FREQ_MS;
    rocket->config.simulation_mode_enabled = DEFAULT_SIMULATION_MODE_ENABLED;

    // Sensor configuration
    rocket->config.accelerometer_range = DEFAULT_ACCELEROMETER_RANGE;

    // Sensor safety
    rocket->config.sensor_timeout_ms = DEFAULT_SENSOR_TIMEOUT_MS;

    // Arming interlock
    rocket->config.require_gps_lock = DEFAULT_REQUIRE_GPS_LOCK;
    rocket->config.arming_altitude_max_delta = DEFAULT_ARMING_ALTITUDE_MAX_DELTA;
    rocket->config.arming_stable_time_ms = DEFAULT_ARMING_STABLE_TIME_MS;

    // Multi-channel pyro
    rocket->config.pyro_enable = DEFAULT_PYRO_ENABLE;
    rocket->config.pyro_drogue_channel = DEFAULT_PYRO_DROGUE_CHANNEL;
    rocket->config.pyro_main_channel = DEFAULT_PYRO_MAIN_CHANNEL;
    rocket->config.pyro_separation_channel = DEFAULT_PYRO_SEPARATION_CHANNEL;
    rocket->config.pyro_backup_channel = DEFAULT_PYRO_BACKUP_CHANNEL;
    rocket->config.pyro_drogue_duration_ms = DEFAULT_PYRO_DROGUE_DURATION_MS;
    rocket->config.pyro_main_duration_ms = DEFAULT_PYRO_MAIN_DURATION_MS;
    rocket->config.main_deploy_altitude_agl = DEFAULT_MAIN_DEPLOY_ALTITUDE_AGL;

    // Improved apogee detection
    rocket->config.apogee_velocity_threshold = DEFAULT_APOGEE_VELOCITY_THRESHOLD;
    rocket->config.apogee_altitude_drop_threshold = DEFAULT_APOGEE_ALTITUDE_DROP_THRESHOLD;

    // Backup parachute deployment (safety)
    rocket->config.backup_activation_delay_ms = DEFAULT_BACKUP_ACTIVATION_DELAY_MS;
    rocket->config.backup_velocity_threshold = DEFAULT_BACKUP_VELOCITY_THRESHOLD;

    SDLogger_WriteText(&sdlogger, "logs/config_loaded_defaults.txt");
}

bool RocketStateMachine_LoadConfig(RocketStateMachine_t* rocket) {
    if (!rocket) return false;

    // Cargar valores por defecto primero
    RocketStateMachine_LoadDefaultConfig(rocket);

    // Intentar leer archivo de configuración desde SD
    if (!sdlogger.is_mounted) {
        SDLogger_WriteText(&sdlogger, "logs/config_no_sd.txt");
        return false;
    }

    FRESULT fr;
    FIL config_file;

    // Intentar abrir archivo de configuración en raíz
    fr = f_open(&config_file, "rocket_config.txt", FA_READ);
    if (fr != FR_OK) {
        SDLogger_WriteText(&sdlogger, "logs/config_file_not_found.txt");
        // Crear archivo de configuración por defecto
        fr = f_open(&config_file, "rocket_config.txt", FA_CREATE_NEW | FA_WRITE);
        if (fr == FR_OK) {
            char config_content[500];
            sprintf(config_content,
                "# Rocket Configuration File\n"
                "# Edit values below and reboot to apply\n"
                "LAUNCH_DETECTION_THRESHOLD=%ld.%ld\n"
                "COAST_DETECTION_THRESHOLD=%ld.%ld\n"
                "BOOST_TIMEOUT_MS=%ld\n"
                "COAST_TIMEOUT_MS=%ld\n"
                "ALTITUDE_STABLE_THRESHOLD=%ld.%ld\n"
                "STABLE_TIME_LANDING_MS=%ld\n"
                "SLEEP_TIMEOUT_MS=%ld\n"
                "DATA_LOGGING_FREQ_MS=%ld\n"
                "SIMULATION_MODE=%s\n",
                (int32_t)(rocket->config.launch_detection_threshold),
                (int32_t)(rocket->config.launch_detection_threshold * 10) % 10,
                (int32_t)(rocket->config.coast_detection_threshold),
                (int32_t)(rocket->config.coast_detection_threshold * 10) % 10,
                rocket->config.boost_timeout_ms,
                rocket->config.coast_timeout_ms,
                (int32_t)(rocket->config.altitude_stable_threshold),
                (int32_t)(rocket->config.altitude_stable_threshold * 10) % 10,
                rocket->config.stable_time_landing_ms,
                rocket->config.sleep_timeout_ms,
                rocket->config.data_logging_frequency_ms,
                rocket->config.simulation_mode_enabled ? "true" : "false"
            );

            UINT bytes_written;
            f_write(&config_file, config_content, strlen(config_content), &bytes_written);
            f_close(&config_file);
            SDLogger_WriteText(&sdlogger, "logs/config_default_created.txt");
        }
        return true; // Usar valores por defecto
    }

    // Leer archivo línea por línea
    char line[100];
    while (f_gets(line, sizeof(line), &config_file)) {
        // Ignorar comentarios y líneas vacías
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        // Parsear configuración
        if (strncmp(line, "LAUNCH_DETECTION_THRESHOLD=", 27) == 0) {
            rocket->config.launch_detection_threshold = atof(line + 27);
        }
        else if (strncmp(line, "COAST_DETECTION_THRESHOLD=", 26) == 0) {
            rocket->config.coast_detection_threshold = atof(line + 26);
        }
        else if (strncmp(line, "BOOST_TIMEOUT_MS=", 17) == 0) {
            rocket->config.boost_timeout_ms = atol(line + 17);
        }
        else if (strncmp(line, "COAST_TIMEOUT_MS=", 17) == 0) {
            rocket->config.coast_timeout_ms = atol(line + 17);
        }
        else if (strncmp(line, "ALTITUDE_STABLE_THRESHOLD=", 26) == 0) {
            rocket->config.altitude_stable_threshold = atof(line + 26);
        }
        else if (strncmp(line, "STABLE_TIME_LANDING_MS=", 23) == 0) {
            rocket->config.stable_time_landing_ms = atol(line + 23);
        }
        else if (strncmp(line, "SLEEP_TIMEOUT_MS=", 17) == 0) {
            rocket->config.sleep_timeout_ms = atol(line + 17);
        }
        else if (strncmp(line, "DATA_LOGGING_FREQ_MS=", 21) == 0) {
            rocket->config.data_logging_frequency_ms = atol(line + 21);
        }
        else if (strncmp(line, "SIMULATION_MODE=", 16) == 0) {
            char* value = line + 16;
            while (*value == ' ') value++;
            if (strncmp(value, "true", 4) == 0) {
                rocket->config.simulation_mode_enabled = true;
            } else {
                rocket->config.simulation_mode_enabled = false;
            }
        }
        // Sensor configuration parameters
        else if (strncmp(line, "ACCELEROMETER_RANGE=", 20) == 0) {
            int range = atoi(line + 20);
            if (range >= 0 && range <= 3) {
                rocket->config.accelerometer_range = (uint8_t)range;
            }
        }
        // Sensor safety parameters
        else if (strncmp(line, "SENSOR_TIMEOUT_MS=", 18) == 0) {
            rocket->config.sensor_timeout_ms = atol(line + 18);
        }
        // Arming interlock parameters
        else if (strncmp(line, "REQUIRE_GPS_LOCK=", 17) == 0) {
            char* value = line + 17;
            while (*value == ' ') value++;
            rocket->config.require_gps_lock = (strncmp(value, "true", 4) == 0);
        }
        else if (strncmp(line, "ARMING_ALTITUDE_MAX_DELTA=", 26) == 0) {
            rocket->config.arming_altitude_max_delta = atof(line + 26);
        }
        else if (strncmp(line, "ARMING_STABLE_TIME_MS=", 22) == 0) {
            rocket->config.arming_stable_time_ms = atol(line + 22);
        }
        // Multi-channel pyro parameters
        else if (strncmp(line, "PYRO_ENABLE=", 12) == 0) {
            char* value = line + 12;
            while (*value == ' ') value++;
            rocket->config.pyro_enable = (strncmp(value, "true", 4) == 0);
        }
        else if (strncmp(line, "PYRO_DROGUE_CHANNEL=", 20) == 0) {
            rocket->config.pyro_drogue_channel = atoi(line + 20);
        }
        else if (strncmp(line, "PYRO_MAIN_CHANNEL=", 18) == 0) {
            rocket->config.pyro_main_channel = atoi(line + 18);
        }
        else if (strncmp(line, "PYRO_SEPARATION_CHANNEL=", 24) == 0) {
            rocket->config.pyro_separation_channel = atoi(line + 24);
        }
        else if (strncmp(line, "PYRO_BACKUP_CHANNEL=", 20) == 0) {
            rocket->config.pyro_backup_channel = atoi(line + 20);
        }
        else if (strncmp(line, "PYRO_DROGUE_DURATION_MS=", 24) == 0) {
            rocket->config.pyro_drogue_duration_ms = atol(line + 24);
        }
        else if (strncmp(line, "PYRO_MAIN_DURATION_MS=", 22) == 0) {
            rocket->config.pyro_main_duration_ms = atol(line + 22);
        }
        else if (strncmp(line, "MAIN_DEPLOY_ALTITUDE_AGL=", 25) == 0) {
            rocket->config.main_deploy_altitude_agl = atof(line + 25);
        }
        // Improved apogee detection
        else if (strncmp(line, "APOGEE_VELOCITY_THRESHOLD=", 26) == 0) {
            rocket->config.apogee_velocity_threshold = atof(line + 26);
        }
        else if (strncmp(line, "APOGEE_ALTITUDE_DROP_THRESHOLD=", 31) == 0) {
            rocket->config.apogee_altitude_drop_threshold = atof(line + 31);
        }
        // Backup parachute deployment (safety)
        else if (strncmp(line, "BACKUP_ACTIVATION_DELAY_MS=", 27) == 0) {
            rocket->config.backup_activation_delay_ms = atol(line + 27);
        }
        else if (strncmp(line, "BACKUP_VELOCITY_THRESHOLD=", 26) == 0) {
            rocket->config.backup_velocity_threshold = atof(line + 26);
        }
    }

    f_close(&config_file);

    char config_msg[250];
    sprintf(config_msg, "Config: Launch=%ld.%ldG, Coast=%ld.%ldG, BoostTO=%ldms, CoastTO=%ldms, Stable=%ld.%ldm, Landing=%ldms, Sim=%s",
           (int32_t)(rocket->config.launch_detection_threshold),
           (int32_t)(rocket->config.launch_detection_threshold * 10) % 10,
           (int32_t)(rocket->config.coast_detection_threshold),
           (int32_t)(rocket->config.coast_detection_threshold * 10) % 10,
           rocket->config.boost_timeout_ms,
           rocket->config.coast_timeout_ms,
           (int32_t)(rocket->config.altitude_stable_threshold),
           (int32_t)(rocket->config.altitude_stable_threshold * 10) % 10,
           rocket->config.stable_time_landing_ms,
           rocket->config.simulation_mode_enabled ? "ON" : "OFF");
    SDLogger_WriteText(&sdlogger, config_msg);

    char pyro_msg[100];
    sprintf(pyro_msg, "Pyro Channels: %s", rocket->config.pyro_enable ? "ENABLED" : "DISABLED");
    SDLogger_WriteText(&sdlogger, pyro_msg);

    char backup_msg[150];
    sprintf(backup_msg, "Backup Parachute: Delay=%ldms, VelThreshold=%ld.%ldm/s",
           rocket->config.backup_activation_delay_ms,
           (int32_t)(rocket->config.backup_velocity_threshold),
           abs((int32_t)(rocket->config.backup_velocity_threshold * 10) % 10));
    SDLogger_WriteText(&sdlogger, backup_msg);

    return true;
}
