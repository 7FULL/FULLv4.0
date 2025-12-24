#include "ZOE_M8Q.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Función auxiliar para convertir NMEA a decimal
static double NMEA_ToDecimalDegrees(double nmea_coord, char direction) {
    int degrees = (int)(nmea_coord / 100);
    double minutes = nmea_coord - (degrees * 100);
    double decimal = degrees + (minutes / 60.0);

    if (direction == 'S' || direction == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

// Función auxiliar para calcular checksum NMEA
static uint8_t NMEA_CalculateChecksum(const char* sentence) {
    uint8_t checksum = 0;
    int i = 1; // Saltar el '$'

    while (sentence[i] != '*' && sentence[i] != '\0') {
        checksum ^= sentence[i];
        i++;
    }

    return checksum;
}

void ZOE_M8Q_Reset(void) {
    // Reset activo LOW
    HAL_GPIO_WritePin(ZOE_M8Q_RESET_GPIO_PORT, ZOE_M8Q_RESET_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(ZOE_M8Q_RESET_GPIO_PORT, ZOE_M8Q_RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(100); // Esperar a que el GPS se reinicie
}

void ZOE_M8Q_SendImpulse(void) {
    // Pulso en el pin IMPULSE para despertar el GPS si está en modo sleep
    HAL_GPIO_WritePin(ZOE_M8Q_IMPULSE_GPIO_PORT, ZOE_M8Q_IMPULSE_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(ZOE_M8Q_IMPULSE_GPIO_PORT, ZOE_M8Q_IMPULSE_PIN, GPIO_PIN_RESET);
}

bool ZOE_M8Q_Init(ZOE_M8Q_t *gps, I2C_HandleTypeDef *hi2c) {
    if (!gps || !hi2c) return false;

    gps->hi2c = hi2c;
    gps->is_initialized = false;
    gps->buffer_index = 0;
    memset(&gps->gps_data, 0, sizeof(ZOE_M8Q_Data_t));
    memset(gps->nmea_buffer, 0, sizeof(gps->nmea_buffer));

    // Configurar pines de control
    HAL_GPIO_WritePin(ZOE_M8Q_RESET_GPIO_PORT, ZOE_M8Q_RESET_PIN, GPIO_PIN_SET);      // Reset inactivo (HIGH)
    HAL_GPIO_WritePin(ZOE_M8Q_IMPULSE_GPIO_PORT, ZOE_M8Q_IMPULSE_PIN, GPIO_PIN_RESET); // Impulse inactivo (LOW)

    // Realizar reset del GPS
    ZOE_M8Q_Reset();

    // Enviar impulso para asegurar que está despierto
    ZOE_M8Q_SendImpulse();

    // Test de comunicación I2C (reintentos porque puede tardar en estar listo)
    bool i2c_ready = false;
    for (int i = 0; i < 10; i++) {
        if (HAL_I2C_IsDeviceReady(gps->hi2c, ZOE_M8Q_I2C_ADDR << 1, 5, 200) == HAL_OK) {
            i2c_ready = true;
            break;
        }
        HAL_Delay(100);
    }

    if (!i2c_ready) {
        return false;
    }

    gps->is_initialized = true;
    return true;
}

bool ZOE_M8Q_IsDataAvailable(ZOE_M8Q_t *gps) {
    if (!gps || !gps->is_initialized) return false;

    uint8_t data_length[2];

    // Leer los registros de longitud de datos
    if (HAL_I2C_Mem_Read(gps->hi2c, ZOE_M8Q_I2C_ADDR << 1,
                         ZOE_M8Q_REG_DATA_LENGTH_H, I2C_MEMADD_SIZE_8BIT,
                         data_length, 2, 100) != HAL_OK) {
        return false;
    }

    uint16_t available_bytes = (data_length[0] << 8) | data_length[1];
    return (available_bytes > 0);
}

bool ZOE_M8Q_ReadData(ZOE_M8Q_t *gps) {
    if (!gps || !gps->is_initialized) return false;

    uint8_t data_length[2];

    // Leer longitud de datos disponibles
    if (HAL_I2C_Mem_Read(gps->hi2c, ZOE_M8Q_I2C_ADDR << 1,
                         ZOE_M8Q_REG_DATA_LENGTH_H, I2C_MEMADD_SIZE_8BIT,
                         data_length, 2, 100) != HAL_OK) {
        return false;
    }

    uint16_t available_bytes = (data_length[0] << 8) | data_length[1];
    if (available_bytes == 0) return false;

    // Limitar la lectura al tamaño del buffer
    if (available_bytes > sizeof(gps->nmea_buffer) - 1) {
        available_bytes = sizeof(gps->nmea_buffer) - 1;
    }

    // Leer los datos
    uint8_t temp_buffer[256];
    if (HAL_I2C_Mem_Read(gps->hi2c, ZOE_M8Q_I2C_ADDR << 1,
                         ZOE_M8Q_REG_DATA_STREAM, I2C_MEMADD_SIZE_8BIT,
                         temp_buffer, available_bytes, 200) != HAL_OK) {
        return false;
    }

    // Procesar datos byte a byte buscando sentencias NMEA completas
    for (uint16_t i = 0; i < available_bytes; i++) {
        char received_char = temp_buffer[i];

        if (received_char == '$') {
            // Inicio de nueva sentencia NMEA
            gps->buffer_index = 0;
            gps->nmea_buffer[gps->buffer_index++] = received_char;
        } else if (received_char == '\n' || received_char == '\r') {
            // Final de sentencia NMEA
            if (gps->buffer_index > 0) {
                gps->nmea_buffer[gps->buffer_index] = '\0';

                // Procesar la sentencia NMEA completa
                if (ZOE_M8Q_ParseNMEA(gps, gps->nmea_buffer)) {
                    gps->buffer_index = 0;
                    return true; // Datos válidos procesados
                }
                gps->buffer_index = 0;
            }
        } else if (gps->buffer_index < sizeof(gps->nmea_buffer) - 1) {
            // Agregar carácter al buffer
            gps->nmea_buffer[gps->buffer_index++] = received_char;
        }
    }

    return false;
}

bool ZOE_M8Q_ParseNMEA(ZOE_M8Q_t *gps, char *nmea_sentence) {
    if (!gps || !nmea_sentence) return false;

    // Verificar que es una sentencia NMEA válida
    if (nmea_sentence[0] != '$') return false;

    // Buscar el checksum
    char *checksum_ptr = strrchr(nmea_sentence, '*');
    if (!checksum_ptr) return false;

    // Verificar checksum
    uint8_t calculated_checksum = NMEA_CalculateChecksum(nmea_sentence);
    uint8_t received_checksum = strtol(checksum_ptr + 1, NULL, 16);

    if (calculated_checksum != received_checksum) return false;

    // Parsear diferentes tipos de sentencias NMEA
    if (strncmp(nmea_sentence, "$GNGGA", 6) == 0 || strncmp(nmea_sentence, "$GPGGA", 6) == 0) {
        // GGA - Global Positioning System Fix Data
        char *token = strtok(nmea_sentence, ",");
        int field = 0;
        double lat_raw = 0, lon_raw = 0;
        char lat_dir = 'N', lon_dir = 'E';

        while (token != NULL && field < 15) {
            switch (field) {
                case 1: // Time
                    if (strlen(token) >= 6) {
                        gps->gps_data.hour = (token[0] - '0') * 10 + (token[1] - '0');
                        gps->gps_data.minute = (token[2] - '0') * 10 + (token[3] - '0');
                        gps->gps_data.second = (token[4] - '0') * 10 + (token[5] - '0');
                    }
                    break;
                case 2: // Latitude
                    if (strlen(token) > 0) {
                        lat_raw = atof(token);
                    }
                    break;
                case 3: // Latitude direction
                    lat_dir = token[0];
                    break;
                case 4: // Longitude
                    if (strlen(token) > 0) {
                        lon_raw = atof(token);
                    }
                    break;
                case 5: // Longitude direction
                    lon_dir = token[0];
                    break;
                case 6: // Fix quality
                    gps->gps_data.fix_valid = (atoi(token) > 0);
                    break;
                case 7: // Number of satellites
                    gps->gps_data.satellites_used = atoi(token);
                    break;
                case 8: // Horizontal dilution
                    gps->gps_data.hdop = atof(token);
                    break;
                case 9: // Altitude
                    gps->gps_data.altitude = atof(token);
                    break;
            }
            token = strtok(NULL, ",");
            field++;
        }

        // Convertir coordenadas a grados decimales
        if (lat_raw != 0) {
            gps->gps_data.latitude = NMEA_ToDecimalDegrees(lat_raw, lat_dir);
        }
        if (lon_raw != 0) {
            gps->gps_data.longitude = NMEA_ToDecimalDegrees(lon_raw, lon_dir);
        }

        gps->gps_data.last_update = HAL_GetTick();
        return true;
    }
    else if (strncmp(nmea_sentence, "$GNRMC", 6) == 0 || strncmp(nmea_sentence, "$GPRMC", 6) == 0) {
        // RMC - Recommended Minimum Specific GPS/Transit Data
        char *token = strtok(nmea_sentence, ",");
        int field = 0;

        while (token != NULL && field < 12) {
            switch (field) {
                case 2: // Status
                    gps->gps_data.fix_valid = (token[0] == 'A');
                    break;
                case 7: // Speed over ground in knots
                    gps->gps_data.speed_kmh = atof(token) * 1.852f; // Convertir a km/h
                    break;
                case 8: // Course over ground
                    gps->gps_data.heading = atof(token);
                    break;
                case 9: // Date
                    if (strlen(token) >= 6) {
                        gps->gps_data.day = (token[0] - '0') * 10 + (token[1] - '0');
                        gps->gps_data.month = (token[2] - '0') * 10 + (token[3] - '0');
                        gps->gps_data.year = 2000 + (token[4] - '0') * 10 + (token[5] - '0');
                    }
                    break;
            }
            token = strtok(NULL, ",");
            field++;
        }

        return true;
    }

    return false;
}

bool ZOE_M8Q_GetLatestData(ZOE_M8Q_t *gps, ZOE_M8Q_Data_t *data_out) {
    if (!gps || !gps->is_initialized || !data_out) return false;

    memcpy(data_out, &gps->gps_data, sizeof(ZOE_M8Q_Data_t));
    return true;
}

bool ZOE_M8Q_HasValidFix(ZOE_M8Q_t *gps) {
    if (!gps || !gps->is_initialized) return false;

    return gps->gps_data.fix_valid && (gps->gps_data.satellites_used > 3);
}

uint32_t ZOE_M8Q_GetTimeSinceLastUpdate(ZOE_M8Q_t *gps) {
    if (!gps || !gps->is_initialized) return 0xFFFFFFFF;

    return HAL_GetTick() - gps->gps_data.last_update;
}

void ZOE_M8Q_GetLocationString(ZOE_M8Q_Data_t *data, char *buffer, size_t buffer_size) {
    if (!data || !buffer) return;

    // Convertir a enteros para evitar float formatting
    int32_t lat_int = (int32_t)(data->latitude * 1000000);
    int32_t lon_int = (int32_t)(data->longitude * 1000000);
    int32_t alt_int = (int32_t)(data->altitude * 100);

    snprintf(buffer, buffer_size,
             "LAT=%ld.%06ld LON=%ld.%06ld ALT=%ld.%02ldm",
             lat_int/1000000, abs(lat_int%1000000),
             lon_int/1000000, abs(lon_int%1000000),
             alt_int/100, abs(alt_int%100));
}

void ZOE_M8Q_GetTimeString(ZOE_M8Q_Data_t *data, char *buffer, size_t buffer_size) {
    if (!data || !buffer) return;

    snprintf(buffer, buffer_size,
             "%04d-%02d-%02d %02d:%02d:%02d",
             data->year, data->month, data->day,
             data->hour, data->minute, data->second);
}

void ZOE_M8Q_GetStatusString(ZOE_M8Q_Data_t *data, char *buffer, size_t buffer_size) {
    if (!data || !buffer) return;

    int32_t speed_int = (int32_t)(data->speed_kmh * 10);
    int32_t hdop_int = (int32_t)(data->hdop * 100);
    int32_t heading_int = (int32_t)(data->heading * 10);

    snprintf(buffer, buffer_size,
             "FIX=%s SAT=%d HDOP=%ld.%02ld SPD=%ld.%01ldkm/h HDG=%ld.%01ld°",
             data->fix_valid ? "OK" : "NO",
             data->satellites_used,
             hdop_int/100, abs(hdop_int%100),
             speed_int/10, abs(speed_int%10),
             heading_int/10, abs(heading_int%10));
}