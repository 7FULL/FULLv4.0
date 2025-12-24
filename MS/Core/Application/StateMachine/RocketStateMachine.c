#include "RocketStateMachine.h"
#include "SDLogger.h"
#include <string.h>
#include <stdio.h>

// Valores por defecto - serán sobrescritos por configuración de SD
#define DEFAULT_LAUNCH_DETECTION_THRESHOLD 2.5f    // 2.5G acceleration threshold
#define DEFAULT_COAST_DETECTION_THRESHOLD  1.5f    // 1.5G coast detection
#define DEFAULT_APOGEE_DESCENT_TIME_MS     5000    // 5 seconds of descent to confirm apogee
#define DEFAULT_ALTITUDE_STABLE_THRESHOLD  2.0f    // 2m difference to consider stable
#define DEFAULT_STABLE_TIME_LANDING_MS     8000    // 8 seconds stable altitude to confirm landing
#define DEFAULT_SLEEP_TIMEOUT_MS          10000    // 10 seconds in sleep before arming
#define DEFAULT_DATA_LOGGING_FREQ_MS         10    // 10ms = 100Hz logging frequency
#define DEFAULT_SIMULATION_MODE_ENABLED   false   // Simulation mode disabled by default
#define PYRO_CHANNEL1_DURATION_MS         3000   // 3 segundos de activación

extern SDLogger_t sdlogger;

static const char* state_names[] = {
    "SLEEP",
    "ARMED",
    "BOOST",
    "COAST",
    "APOGEE",
    "PARACHUTE",
    "LANDED"
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

    rocket->sensors_initialized = true;
    rocket->data_logging_active = false;
    rocket->simulation_mode = false;
    rocket->total_data_points = 0;
    rocket->spi_write_address = 0x000000;

    if (!RocketStateMachine_ReadSensors(rocket)) {
        return false;
    }

    rocket->ground_altitude = rocket->current_data.altitude;
    rocket->max_altitude = rocket->ground_altitude;
    rocket->apogee_altitude = rocket->ground_altitude;
    rocket->last_altitude = rocket->current_data.altitude;
    rocket->stable_altitude_start_time = HAL_GetTick();

    // Inicializar variables de pyrochannel
    rocket->pyro_channel1_active = false;
    rocket->pyro_channel1_start_time = 0;

    // Cargar configuración desde SD
    RocketStateMachine_LoadConfig(rocket);

    // Aplicar configuración de simulación
    rocket->simulation_mode = rocket->config.simulation_mode_enabled;

    RocketStateMachine_UpdateLED(rocket);
    RocketStateMachine_UpdateBuzzer(rocket);

    char init_msg[100];
    sprintf(init_msg, "ROCKET: Initialized at altitude: %ld.%02dm",
           (int32_t)(rocket->ground_altitude),
           (int32_t)(rocket->ground_altitude * 100) % 100);
    SDLogger_WriteText(&sdlogger, init_msg);

    return true;
}

void RocketStateMachine_Update(RocketStateMachine_t* rocket) {
    if (!rocket || !rocket->sensors_initialized) {
        return;
    }

    if (!RocketStateMachine_ReadSensors(rocket)) {
        return;
    }

    RocketState_t next_state = rocket->current_state;
    uint32_t time_in_state = HAL_GetTick() - rocket->state_start_time;

    switch (rocket->current_state) {
        case ROCKET_STATE_SLEEP:
            if (time_in_state > rocket->config.sleep_timeout_ms) {
                next_state = ROCKET_STATE_ARMED;
            }
            break;

        case ROCKET_STATE_ARMED:
            if (rocket->current_data.acceleration_x > rocket->config.launch_detection_threshold) {
                next_state = ROCKET_STATE_BOOST;
                SDLogger_WriteText(&sdlogger, "logs/launch_detected.txt");
            }
            break;

        case ROCKET_STATE_BOOST:
            if (rocket->current_data.acceleration_x < rocket->config.coast_detection_threshold) {
                next_state = ROCKET_STATE_COAST;
            }
            break;

        case ROCKET_STATE_COAST:
            if (rocket->current_data.altitude > rocket->max_altitude) {
                rocket->max_altitude = rocket->current_data.altitude;
            }

            if (rocket->current_data.altitude < (rocket->max_altitude - 5.0f) ||
                time_in_state > rocket->config.apogee_descent_time_ms) {
                rocket->apogee_altitude = rocket->max_altitude;
                next_state = ROCKET_STATE_APOGEE;
            }
            break;

        case ROCKET_STATE_APOGEE:
            next_state = ROCKET_STATE_PARACHUTE;
            break;

        case ROCKET_STATE_PARACHUTE:
            // Comprobar si la altitud se ha estabilizado (no está descendiendo)
            if (abs(rocket->current_data.altitude - rocket->last_altitude) < rocket->config.altitude_stable_threshold) {
                // Altitud estable, verificar si llevamos suficiente tiempo así
                uint32_t stable_time = HAL_GetTick() - rocket->stable_altitude_start_time;
                if (stable_time > rocket->config.stable_time_landing_ms) {
                    next_state = ROCKET_STATE_LANDED;
                }
            } else {
                // Altitud cambió significativamente, reiniciar contador de estabilidad
                rocket->last_altitude = rocket->current_data.altitude;
                rocket->stable_altitude_start_time = HAL_GetTick();
            }
            break;

        case ROCKET_STATE_LANDED:
            if (!rocket->data_logging_active) {
                static bool transfer_completed = false;
                if (!transfer_completed) {
                    transfer_completed = RocketStateMachine_TransferDataToSD(rocket);
                    if (transfer_completed) {
                        // Solo borrar el flash si la transferencia fue exitosa desde LANDED
                        RocketStateMachine_EraseFlashData(rocket);
                        SDLogger_WriteText(&sdlogger, "Vuelo completado - Flash limpiado para próximo vuelo");
                    }
                }
            }
            break;
    }

    if (next_state != rocket->current_state) {
        RocketStateMachine_ChangeState(rocket, next_state);
    }

    if (rocket->data_logging_active && rocket->current_state != ROCKET_STATE_LANDED) {
        RocketStateMachine_LogData(rocket);
    }

    // Verificar si debe desactivar pyrochannel 1
    if (rocket->pyro_channel1_active) {
        uint32_t elapsed_time = HAL_GetTick() - rocket->pyro_channel1_start_time;
        if (elapsed_time >= PYRO_CHANNEL1_DURATION_MS) {
            rocket->pyro_channel1_active = false;
            PyroChannels_DeactivateChannel(0);
            SDLogger_WriteText(&sdlogger, "logs/pyrochannel1_deactivated.txt");
        }
    }

    RocketStateMachine_UpdateLED(rocket);
    RocketStateMachine_UpdateBuzzer(rocket);
}

void RocketStateMachine_ChangeState(RocketStateMachine_t* rocket, RocketState_t new_state) {
    if (!rocket || new_state == rocket->current_state) {
        return;
    }

    // Solo escribir cambios de estado a SD durante inicialización
    // Durante vuelo, solo se guarda en Flash
    if (new_state == ROCKET_STATE_ARMED) {
        char state_msg[100];
        sprintf(state_msg, "STATE CHANGE: %s -> %s",
               state_names[rocket->current_state],
               state_names[new_state]);
        SDLogger_WriteText(&sdlogger, state_msg);

        // Activar logging desde ARMED
        rocket->data_logging_active = true;
        SDLogger_WriteText(&sdlogger, "logs/data_logging_started.txt");
    }

    rocket->previous_state = rocket->current_state;
    rocket->current_state = new_state;
    rocket->state_start_time = HAL_GetTick();

    // Activar pyrochannel 1 cuando pase a PARACHUTE
    if (new_state == ROCKET_STATE_PARACHUTE) {
        rocket->pyro_channel1_active = true;
        rocket->pyro_channel1_start_time = HAL_GetTick();
        PyroChannels_ActivateChannel(0);  // Canal 1 = index 0
        SDLogger_WriteText(&sdlogger, "logs/pyrochannel1_activated.txt");
    }

    if (new_state == ROCKET_STATE_LANDED) {
        rocket->data_logging_active = false;
        char landing_msg[100];
        sprintf(landing_msg, "FLIGHT COMPLETE: Max altitude: %ld.%02dm, Data points: %ld",
               (int32_t)(rocket->max_altitude),
               (int32_t)(rocket->max_altitude * 100) % 100,
               rocket->total_data_points);
        SDLogger_WriteText(&sdlogger, landing_msg);
    }
}

const char* RocketStateMachine_GetStateName(RocketState_t state) {
    if (state >= 0 && state < (sizeof(state_names) / sizeof(state_names[0]))) {
        return state_names[state];
    }
    return "UNKNOWN";
}

bool RocketStateMachine_ReadSensors(RocketStateMachine_t* rocket) {
    if (!rocket) return false;

    rocket->current_data.timestamp = HAL_GetTick();

    // Solo leer acelerómetro si NO estamos en modo simulación
    if (!rocket->simulation_mode) {
        KX134_AccelData_t accel_data;
        if (KX134_ReadAccelG(rocket->accelerometer, &accel_data)) {
            rocket->current_data.acceleration_x = accel_data.x;
            rocket->current_data.acceleration_y = accel_data.y;
            rocket->current_data.acceleration_z = accel_data.z;
        }
    }
    // En modo simulación, los valores de aceleración ya fueron modificados por SimulateLaunchSequence()
    // NO los sobrescribimos aquí, solo aseguramos que las velocidades angulares sean 0

    // KX134 no tiene giroscopio, establecer velocidades angulares a 0 siempre
    rocket->current_data.angular_velocity_x = 0.0f;
    rocket->current_data.angular_velocity_y = 0.0f;
    rocket->current_data.angular_velocity_z = 0.0f;

    // Siempre leer presión y temperatura, pero altitud solo si no estamos simulando
    MS5611_Data_t ms_data;
    if (MS5611_ReadData(rocket->barometer, &ms_data)) {
        rocket->current_data.pressure = ms_data.pressure;
        rocket->current_data.temperature = ms_data.temperature;

        // Solo actualizar altitud si NO estamos en modo simulación
        if (!rocket->simulation_mode) {
            rocket->current_data.altitude = (float)ms_data.altitude;
        }
    }

    if (rocket->gps) {
        ZOE_M8Q_ReadData(rocket->gps);
        if (ZOE_M8Q_HasValidFix(rocket->gps)) {
            rocket->current_data.latitude = rocket->gps->gps_data.latitude;
            rocket->current_data.longitude = rocket->gps->gps_data.longitude;
            rocket->current_data.gps_altitude = rocket->gps->gps_data.altitude;
        }
    }

    // Incluir el estado actual del cohete en los datos
    rocket->current_data.rocket_state = rocket->current_state;

    // Capturar el estado de los canales pirotécnicos (bit 0-3 para canales 0-3)
    rocket->current_data.pyro_channel_states = 0;
    for (uint8_t ch = 0; ch < 4; ch++) {
        if (PyroChannels_IsChannelActive(ch)) {
            rocket->current_data.pyro_channel_states |= (1 << ch);
        }
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

    switch (rocket->current_state) {
        case ROCKET_STATE_SLEEP:
            WS2812B_SetColorRGB(rocket->status_led, 128, 0, 128); // Morado
            break;
        case ROCKET_STATE_ARMED:
            WS2812B_SetColorRGB(rocket->status_led, 255, 255, 0); // Amarillo
            break;
        case ROCKET_STATE_BOOST:
            WS2812B_SetColorRGB(rocket->status_led, 255, 0, 0);   // Rojo
            break;
        case ROCKET_STATE_COAST:
            WS2812B_SetColorRGB(rocket->status_led, 0, 0, 255);   // Azul
            break;
        case ROCKET_STATE_APOGEE:
            WS2812B_SetColorRGB(rocket->status_led, 255, 255, 255); // Blanco
            break;
        case ROCKET_STATE_PARACHUTE:
            WS2812B_SetColorRGB(rocket->status_led, 0, 255, 255); // Cyan
            break;
        case ROCKET_STATE_LANDED:
            WS2812B_SetColorRGB(rocket->status_led, 0, 255, 0);   // Verde
            break;
        default:
            WS2812B_SetColorRGB(rocket->status_led, 255, 255, 255); // Blanco por defecto
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
                BUZZER_SUCCESS(rocket->buzzer);
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
    char header[] = "Timestamp,AccelX,AccelY,AccelZ,GyroX,GyroY,GyroZ,Pressure,Temperature,Altitude,Latitude,Longitude,GPS_Alt,State,Pyro0,Pyro1,Pyro2,Pyro3\r\n";
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
            sprintf(csv_line, "%ld,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%03d,%ld.%02d,%ld.%02d,%ld.%02d,%ld.%06d,%ld.%06d,%ld.%02d,%s,%d,%d,%d,%d\r\n",
                   flight_data.timestamp,
                   (int32_t)(flight_data.acceleration_x * 1000), abs((int32_t)(flight_data.acceleration_x * 1000) % 1000),
                   (int32_t)(flight_data.acceleration_y * 1000), abs((int32_t)(flight_data.acceleration_y * 1000) % 1000),
                   (int32_t)(flight_data.acceleration_z * 1000), abs((int32_t)(flight_data.acceleration_z * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_x * 1000), abs((int32_t)(flight_data.angular_velocity_x * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_y * 1000), abs((int32_t)(flight_data.angular_velocity_y * 1000) % 1000),
                   (int32_t)(flight_data.angular_velocity_z * 1000), abs((int32_t)(flight_data.angular_velocity_z * 1000) % 1000),
                   (int32_t)(flight_data.pressure * 100), abs((int32_t)(flight_data.pressure * 100) % 100),
                   (int32_t)(flight_data.temperature * 100), abs((int32_t)(flight_data.temperature * 100) % 100),
                   (int32_t)(flight_data.altitude * 100), abs((int32_t)(flight_data.altitude * 100) % 100),
                   (int32_t)(flight_data.latitude * 1000000), abs((int32_t)(flight_data.latitude * 1000000) % 1000000),
                   (int32_t)(flight_data.longitude * 1000000), abs((int32_t)(flight_data.longitude * 1000000) % 1000000),
                   (int32_t)(flight_data.gps_altitude * 100), abs((int32_t)(flight_data.gps_altitude * 100) % 100),
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

        char header[] = "Timestamp,AccelX,AccelY,AccelZ,GyroX,GyroY,GyroZ,Pressure,Temperature,Altitude,Latitude,Longitude,GPS_Alt,Pyro0,Pyro1,Pyro2,Pyro3";

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
                       (int32_t)(flight_data.acceleration_x * 1000), abs((int32_t)(flight_data.acceleration_x * 1000) % 1000),
                       (int32_t)(flight_data.acceleration_y * 1000), abs((int32_t)(flight_data.acceleration_y * 1000) % 1000),
                       (int32_t)(flight_data.acceleration_z * 1000), abs((int32_t)(flight_data.acceleration_z * 1000) % 1000),
                       (int32_t)(flight_data.pressure * 100), abs((int32_t)(flight_data.pressure * 100) % 100),
                       (int32_t)(flight_data.temperature * 100), abs((int32_t)(flight_data.temperature * 100) % 100),
                       (int32_t)(flight_data.altitude * 100), abs((int32_t)(flight_data.altitude * 100) % 100),
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

    // Recuperar los datos
    rocket->total_data_points = data_points;
    if (RocketStateMachine_TransferDataToSD(rocket)) {
        SDLogger_WriteText(&sdlogger, "Datos recuperados exitosamente - Borrando Flash");
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

    rocket->config.launch_detection_threshold = DEFAULT_LAUNCH_DETECTION_THRESHOLD;
    rocket->config.coast_detection_threshold = DEFAULT_COAST_DETECTION_THRESHOLD;
    rocket->config.apogee_descent_time_ms = DEFAULT_APOGEE_DESCENT_TIME_MS;
    rocket->config.altitude_stable_threshold = DEFAULT_ALTITUDE_STABLE_THRESHOLD;
    rocket->config.stable_time_landing_ms = DEFAULT_STABLE_TIME_LANDING_MS;
    rocket->config.sleep_timeout_ms = DEFAULT_SLEEP_TIMEOUT_MS;
    rocket->config.data_logging_frequency_ms = DEFAULT_DATA_LOGGING_FREQ_MS;
    rocket->config.simulation_mode_enabled = DEFAULT_SIMULATION_MODE_ENABLED;

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
                "APOGEE_DESCENT_TIME_MS=%ld\n"
                "ALTITUDE_STABLE_THRESHOLD=%ld.%ld\n"
                "STABLE_TIME_LANDING_MS=%ld\n"
                "SLEEP_TIMEOUT_MS=%ld\n"
                "DATA_LOGGING_FREQ_MS=%ld\n"
                "SIMULATION_MODE=%s\n",
                (int32_t)(rocket->config.launch_detection_threshold),
                (int32_t)(rocket->config.launch_detection_threshold * 10) % 10,
                (int32_t)(rocket->config.coast_detection_threshold),
                (int32_t)(rocket->config.coast_detection_threshold * 10) % 10,
                rocket->config.apogee_descent_time_ms,
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
        else if (strncmp(line, "APOGEE_DESCENT_TIME_MS=", 23) == 0) {
            rocket->config.apogee_descent_time_ms = atol(line + 23);
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
            // Eliminar espacios y saltos de línea
            while (*value == ' ') value++;
            if (strncmp(value, "true", 4) == 0) {
                rocket->config.simulation_mode_enabled = true;
            } else {
                rocket->config.simulation_mode_enabled = false;
            }
        }
    }

    f_close(&config_file);

    char config_msg[250];
    sprintf(config_msg, "Config: Launch=%ld.%ldG, Coast=%ld.%ldG, Apogee=%ldms, Stable=%ld.%ldm, Landing=%ldms, Simulation=%s",
           (int32_t)(rocket->config.launch_detection_threshold),
           (int32_t)(rocket->config.launch_detection_threshold * 10) % 10,
           (int32_t)(rocket->config.coast_detection_threshold),
           (int32_t)(rocket->config.coast_detection_threshold * 10) % 10,
           rocket->config.apogee_descent_time_ms,
           (int32_t)(rocket->config.altitude_stable_threshold),
           (int32_t)(rocket->config.altitude_stable_threshold * 10) % 10,
           rocket->config.stable_time_landing_ms,
           rocket->config.simulation_mode_enabled ? "ON" : "OFF");
    SDLogger_WriteText(&sdlogger, config_msg);

    return true;
}
