#include "SDLogger.h"
#include <stdio.h>
#include <string.h>

static int GetNextDebugFileName(char *filename, size_t maxLen) {
    int index = 1;
    FILINFO fno;
    FRESULT fr;

    do {
        snprintf(filename, maxLen, "logs/DEBUG_%d.txt", index);
        fr = f_stat(filename, &fno);
        if (fr == FR_NO_FILE) {
            return index;
        }
        index++;
    } while (index < 1000);

    return -1;
}

int SDLogger_GetNextFlightFileName(char *filename, size_t maxLen, const char* prefix, const char* folder) {
    int index = 1;
    FILINFO fno;
    FRESULT fr;

    do {
        if (folder && strlen(folder) > 0) {
            snprintf(filename, maxLen, "%s/%s_%d.csv", folder, prefix, index);
        } else {
            snprintf(filename, maxLen, "%s_%d.csv", prefix, index);
        }
        fr = f_stat(filename, &fno);
        if (fr == FR_NO_FILE) {
            return index;
        }
        index++;
    } while (index < 1000);

    return -1;
}

bool SDLogger_Init(SDLogger_t* logger) {
    if (!logger) return false;

    logger->is_mounted = false;
    logger->is_file_open = false;
    memset(logger->filename, 0, sizeof(logger->filename));

    // Mount SD card
    FRESULT result = f_mount(&logger->fatfs, "", 1);
    if (result != FR_OK) {
        return false;
    }

    logger->is_mounted = true;
    return true;
}

bool SDLogger_CreateDebugFile(SDLogger_t* logger) {
    if (!logger || !logger->is_mounted) return false;

    // Get next available debug file name
    if (GetNextDebugFileName(logger->filename, sizeof(logger->filename)) < 0) {
        return false;
    }

    // Create and open the debug file
    FRESULT result = f_open(&logger->file, logger->filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        return false;
    }

    logger->is_file_open = true;
    return true;
}

bool SDLogger_WriteHeader(SDLogger_t* logger, const char* header) {
    if (!logger || !logger->is_file_open || !header) return false;

    UINT bytes_written;
    FRESULT result = f_write(&logger->file, header, strlen(header), &bytes_written);

    if (result == FR_OK) {
        f_sync(&logger->file);
        return true;
    }

    return false;
}

bool SDLogger_WriteSensorData(SDLogger_t* logger, uint8_t kx_id, uint16_t ms_prom0) {
    if (!logger || !logger->is_file_open) return false;

    char buffer[128];
    snprintf(buffer, sizeof(buffer),
             "KX134 ID: 0x%02X | MS5611 PROM[0]: 0x%04X\r\n",
             kx_id, ms_prom0);

    UINT bytes_written;
    FRESULT result = f_write(&logger->file, buffer, strlen(buffer), &bytes_written);

    if (result == FR_OK) {
        f_sync(&logger->file);
        return true;
    }

    return false;
}

bool SDLogger_WriteText(SDLogger_t* logger, const char* text) {
    if (!logger || !logger->is_file_open || !text) return false;

    UINT bytes_written;

    // Write text
    FRESULT result = f_write(&logger->file, text, strlen(text), &bytes_written);
    if (result != FR_OK) {
        // SD card write failed - possibly disconnected
        logger->is_mounted = false;
        logger->is_file_open = false;
        return false;
    }

    // Write newline
    result = f_write(&logger->file, "\r\n", 2, &bytes_written);
    if (result != FR_OK) {
        // SD card write failed - possibly disconnected
        logger->is_mounted = false;
        logger->is_file_open = false;
        return false;
    }

    // Sync to ensure data is written
    result = f_sync(&logger->file);
    if (result != FR_OK) {
        // SD card sync failed - possibly disconnected
        logger->is_mounted = false;
        logger->is_file_open = false;
        return false;
    }

    return true;
}

bool SDLogger_WriteCSVFile(SDLogger_t* logger, const char* filename, const char* header, const char* data) {
    if (!logger || !logger->is_mounted || !filename || !header || !data) return false;

    FIL csv_file;
    FRESULT result = f_open(&csv_file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        return false;
    }

    UINT bytes_written;

    // Write header
    result = f_write(&csv_file, header, strlen(header), &bytes_written);
    if (result != FR_OK) {
        f_close(&csv_file);
        return false;
    }

    // Write newline after header
    result = f_write(&csv_file, "\r\n", 2, &bytes_written);
    if (result != FR_OK) {
        f_close(&csv_file);
        return false;
    }

    // Write data
    result = f_write(&csv_file, data, strlen(data), &bytes_written);
    if (result != FR_OK) {
        f_close(&csv_file);
        return false;
    }

    // Close file
    f_close(&csv_file);
    return true;
}

bool SDLogger_Flush(SDLogger_t* logger) {
    if (!logger || !logger->is_file_open) return false;

    return (f_sync(&logger->file) == FR_OK);
}

bool SDLogger_Close(SDLogger_t* logger) {
    if (!logger) return false;

    if (logger->is_file_open) {
        f_close(&logger->file);
        logger->is_file_open = false;
    }

    return true;
}

bool SDLogger_Deinit(SDLogger_t* logger) {
    if (!logger) return false;

    // Close file if open
    SDLogger_Close(logger);

    // Demount SD card
    if (logger->is_mounted) {
        f_mount(NULL, "", 0);
        logger->is_mounted = false;
    }

    return true;
}