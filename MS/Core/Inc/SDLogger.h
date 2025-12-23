#ifndef SDLOGGER_H
#define SDLOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "fatfs.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    FATFS fatfs;
    FIL file;
    char filename[32];
    bool is_mounted;
    bool is_file_open;
} SDLogger_t;

// Funciones públicas
bool SDLogger_Init(SDLogger_t* logger);
bool SDLogger_CreateDebugFile(SDLogger_t* logger);
bool SDLogger_WriteHeader(SDLogger_t* logger, const char* header);
bool SDLogger_WriteSensorData(SDLogger_t* logger, uint8_t kx_id, uint16_t ms_prom0);
bool SDLogger_WriteText(SDLogger_t* logger, const char* text);
bool SDLogger_WriteCSVFile(SDLogger_t* logger, const char* filename, const char* header, const char* data);
int SDLogger_GetNextFlightFileName(char *filename, size_t maxLen, const char* prefix, const char* folder);
bool SDLogger_Flush(SDLogger_t* logger);
bool SDLogger_Close(SDLogger_t* logger);
bool SDLogger_Deinit(SDLogger_t* logger);

#ifdef __cplusplus
}
#endif

#endif // SDLOGGER_H