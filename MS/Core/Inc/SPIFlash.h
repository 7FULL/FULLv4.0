#ifndef SPIFLASH_H
#define SPIFLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "spi.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Pines de control SPI Flash
#define SPIFLASH_CS_PIN             GPIO_PIN_15     // PC15 - Chip Select
#define SPIFLASH_CS_GPIO_PORT       GPIOC
#define SPIFLASH_WP_PIN             GPIO_PIN_4      // PA4 - Write Protect
#define SPIFLASH_WP_GPIO_PORT       GPIOA
#define SPIFLASH_HOLD_PIN           GPIO_PIN_0      // PC0 - Hold
#define SPIFLASH_HOLD_GPIO_PORT     GPIOC

// Comandos SPI Flash estándar (W25Q series)
#define SPIFLASH_CMD_WRITE_ENABLE       0x06
#define SPIFLASH_CMD_WRITE_DISABLE      0x04
#define SPIFLASH_CMD_READ_STATUS        0x05
#define SPIFLASH_CMD_WRITE_STATUS       0x01
#define SPIFLASH_CMD_READ_DATA          0x03
#define SPIFLASH_CMD_FAST_READ          0x0B
#define SPIFLASH_CMD_PAGE_PROGRAM       0x02
#define SPIFLASH_CMD_SECTOR_ERASE       0x20
#define SPIFLASH_CMD_BLOCK_ERASE_32K    0x52
#define SPIFLASH_CMD_BLOCK_ERASE_64K    0xD8
#define SPIFLASH_CMD_CHIP_ERASE         0xC7
#define SPIFLASH_CMD_POWER_DOWN         0xB9
#define SPIFLASH_CMD_POWER_UP           0xAB
#define SPIFLASH_CMD_JEDEC_ID           0x9F

// Status Register bits
#define SPIFLASH_STATUS_BUSY            0x01
#define SPIFLASH_STATUS_WEL             0x02    // Write Enable Latch

// Tamaños específicos para W25Q128JVS (16MB)
#define SPIFLASH_PAGE_SIZE              256     // 256 bytes por página
#define SPIFLASH_SECTOR_SIZE            4096    // 4KB por sector (4096 sectores total)
#define SPIFLASH_BLOCK_SIZE_32K         32768   // 32KB (512 bloques de 32K)
#define SPIFLASH_BLOCK_SIZE_64K         65536   // 64KB (256 bloques de 64K)
#define SPIFLASH_TOTAL_SIZE_W25Q128     16777216 // 16MB total (128Mbit)
#define SPIFLASH_TOTAL_PAGES            65536   // 65536 páginas de 256 bytes
#define SPIFLASH_TOTAL_SECTORS          4096    // 4096 sectores de 4KB

// Timeouts
#define SPIFLASH_TIMEOUT_MS             1000
#define SPIFLASH_ERASE_TIMEOUT_MS       5000

// Información del chip
typedef struct {
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity;
    uint32_t total_size;        // Tamaño total en bytes
    char chip_name[32];
} SPIFlash_ChipInfo_t;

// Estructura principal
typedef struct {
    SPI_HandleTypeDef *hspi;
    SPIFlash_ChipInfo_t chip_info;
    bool is_initialized;
    bool write_protection_enabled;
    uint32_t current_address;   // Para operaciones secuenciales
} SPIFlash_t;

// Funciones de inicialización
bool SPIFlash_Init(SPIFlash_t *flash, SPI_HandleTypeDef *hspi);
bool SPIFlash_ReadChipInfo(SPIFlash_t *flash);
bool SPIFlash_IsReady(SPIFlash_t *flash);

// Funciones de control
void SPIFlash_ChipSelect(SPIFlash_t *flash, bool select);
void SPIFlash_WriteProtect(SPIFlash_t *flash, bool protect);
void SPIFlash_Hold(SPIFlash_t *flash, bool hold);

// Funciones de comando básicas
bool SPIFlash_WriteEnable(SPIFlash_t *flash);
bool SPIFlash_WriteDisable(SPIFlash_t *flash);
uint8_t SPIFlash_ReadStatus(SPIFlash_t *flash);
bool SPIFlash_WaitForReady(SPIFlash_t *flash, uint32_t timeout_ms);

// Funciones de lectura
bool SPIFlash_ReadData(SPIFlash_t *flash, uint32_t address, uint8_t *data, uint32_t length);
bool SPIFlash_FastRead(SPIFlash_t *flash, uint32_t address, uint8_t *data, uint32_t length);
uint8_t SPIFlash_ReadByte(SPIFlash_t *flash, uint32_t address);

// Funciones de escritura
bool SPIFlash_WritePage(SPIFlash_t *flash, uint32_t address, const uint8_t *data, uint32_t length);
bool SPIFlash_WriteData(SPIFlash_t *flash, uint32_t address, const uint8_t *data, uint32_t length);
bool SPIFlash_WriteByte(SPIFlash_t *flash, uint32_t address, uint8_t data);

// Funciones de borrado
bool SPIFlash_EraseSector(SPIFlash_t *flash, uint32_t address);
bool SPIFlash_EraseBlock32K(SPIFlash_t *flash, uint32_t address);
bool SPIFlash_EraseBlock64K(SPIFlash_t *flash, uint32_t address);
bool SPIFlash_EraseChip(SPIFlash_t *flash);

// Funciones de utilidad
bool SPIFlash_WriteString(SPIFlash_t *flash, uint32_t address, const char *str);
bool SPIFlash_ReadString(SPIFlash_t *flash, uint32_t address, char *str, uint32_t max_length);
bool SPIFlash_WriteStruct(SPIFlash_t *flash, uint32_t address, const void *data, uint32_t size);
bool SPIFlash_ReadStruct(SPIFlash_t *flash, uint32_t address, void *data, uint32_t size);

// Funciones de gestión de direcciones
uint32_t SPIFlash_GetSectorAddress(uint32_t address);
uint32_t SPIFlash_GetBlockAddress(uint32_t address, bool is_64k);
bool SPIFlash_IsAddressValid(SPIFlash_t *flash, uint32_t address);

// Funciones de información
void SPIFlash_GetChipInfoString(SPIFlash_t *flash, char *buffer, size_t buffer_size);
uint32_t SPIFlash_GetTotalSize(SPIFlash_t *flash);
uint32_t SPIFlash_GetFreeSectors(SPIFlash_t *flash); // Requiere implementar tabla de sectores

// Funciones específicas para W25Q128JVS
bool SPIFlash_IsW25Q128(SPIFlash_t *flash);
uint32_t SPIFlash_GetSectorCount(SPIFlash_t *flash);
uint32_t SPIFlash_GetPageCount(SPIFlash_t *flash);
void SPIFlash_GetMemoryMap(SPIFlash_t *flash, char *buffer, size_t buffer_size);

// Funciones de power management
bool SPIFlash_PowerDown(SPIFlash_t *flash);
bool SPIFlash_PowerUp(SPIFlash_t *flash);

// Macros de conveniencia
#define SPIFLASH_CS_LOW(flash)      SPIFlash_ChipSelect(flash, true)
#define SPIFLASH_CS_HIGH(flash)     SPIFlash_ChipSelect(flash, false)

#ifdef __cplusplus
}
#endif

#endif // SPIFLASH_H