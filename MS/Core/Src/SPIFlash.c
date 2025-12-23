#include "SPIFlash.h"

// Tabla de chips conocidos para identificación (optimizada para W25Q128JVS)
static const struct {
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity;
    uint32_t total_size;
    const char *name;
} flash_chips[] = {
    // Winbond W25Q series (0xEF = Winbond)
    {0xEF, 0x40, 0x18, 16777216,  "W25Q128JVS"}, // 16MB - Tu chip específico
    {0xEF, 0x40, 0x17, 8388608,   "W25Q64"},     // 8MB
    {0xEF, 0x40, 0x16, 4194304,   "W25Q32"},     // 4MB
    {0xEF, 0x40, 0x15, 2097152,   "W25Q16"},     // 2MB
    {0xEF, 0x40, 0x14, 1048576,   "W25Q80"},     // 1MB
    {0xEF, 0x70, 0x18, 16777216,  "W25Q128JV"},  // Variante del 128
    // Otros fabricantes
    {0x20, 0x20, 0x18, 16777216,  "M25P128"},    // 16MB Micron
    {0x1F, 0x25, 0x18, 16777216,  "AT25DF128"},  // 16MB Atmel
    {0x00, 0x00, 0x00, 0,         "Unknown"}
};

// Funciones auxiliares de control de pines
void SPIFlash_ChipSelect(SPIFlash_t *flash, bool select) {
    if (!flash) return;
    HAL_GPIO_WritePin(SPIFLASH_CS_GPIO_PORT, SPIFLASH_CS_PIN,
                      select ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_Delay(1); // Pequeña espera para estabilizar
}

void SPIFlash_WriteProtect(SPIFlash_t *flash, bool protect) {
    if (!flash) return;
    HAL_GPIO_WritePin(SPIFLASH_WP_GPIO_PORT, SPIFLASH_WP_PIN,
                      protect ? GPIO_PIN_RESET : GPIO_PIN_SET);
    flash->write_protection_enabled = protect;
}

void SPIFlash_Hold(SPIFlash_t *flash, bool hold) {
    if (!flash) return;
    HAL_GPIO_WritePin(SPIFLASH_HOLD_GPIO_PORT, SPIFLASH_HOLD_PIN,
                      hold ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

// Función para enviar comando SPI
static bool SPIFlash_SendCommand(SPIFlash_t *flash, uint8_t cmd) {
    if (!flash || !flash->hspi) return false;

    SPIFLASH_CS_LOW(flash);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(flash->hspi, &cmd, 1, SPIFLASH_TIMEOUT_MS);
    SPIFLASH_CS_HIGH(flash);

    return (status == HAL_OK);
}

// Función para transacción SPI completa
static bool SPIFlash_Transaction(SPIFlash_t *flash, uint8_t *tx_data, uint8_t *rx_data, uint16_t length) {
    if (!flash || !flash->hspi) return false;

    SPIFLASH_CS_LOW(flash);
    HAL_StatusTypeDef status;

    if (tx_data && rx_data) {
        status = HAL_SPI_TransmitReceive(flash->hspi, tx_data, rx_data, length, SPIFLASH_TIMEOUT_MS);
    } else if (tx_data) {
        status = HAL_SPI_Transmit(flash->hspi, tx_data, length, SPIFLASH_TIMEOUT_MS);
    } else if (rx_data) {
        status = HAL_SPI_Receive(flash->hspi, rx_data, length, SPIFLASH_TIMEOUT_MS);
    } else {
        SPIFLASH_CS_HIGH(flash);
        return false;
    }

    SPIFLASH_CS_HIGH(flash);
    return (status == HAL_OK);
}

bool SPIFlash_Init(SPIFlash_t *flash, SPI_HandleTypeDef *hspi) {
    if (!flash || !hspi) return false;

    memset(flash, 0, sizeof(SPIFlash_t));
    flash->hspi = hspi;

    // Configurar pines de control
    SPIFlash_ChipSelect(flash, false);      // CS HIGH (inactivo)
    SPIFlash_WriteProtect(flash, false);    // WP HIGH (sin protección)
    SPIFlash_Hold(flash, false);            // HOLD HIGH (sin hold)

    HAL_Delay(10); // Esperar estabilización

    // Leer información del chip
    if (!SPIFlash_ReadChipInfo(flash)) {
        return false;
    }

    // Verificar que el chip responde
    if (!SPIFlash_IsReady(flash)) {
        return false;
    }

    flash->is_initialized = true;
    return true;
}

bool SPIFlash_ReadChipInfo(SPIFlash_t *flash) {
    if (!flash || !flash->hspi) return false;

    uint8_t jedec_cmd = SPIFLASH_CMD_JEDEC_ID;
    uint8_t jedec_data[3] = {0};

    SPIFLASH_CS_LOW(flash);
    HAL_StatusTypeDef status1 = HAL_SPI_Transmit(flash->hspi, &jedec_cmd, 1, SPIFLASH_TIMEOUT_MS);
    HAL_StatusTypeDef status2 = HAL_SPI_Receive(flash->hspi, jedec_data, 3, SPIFLASH_TIMEOUT_MS);
    SPIFLASH_CS_HIGH(flash);

    if (status1 != HAL_OK || status2 != HAL_OK) {
        return false;
    }

    flash->chip_info.manufacturer_id = jedec_data[0];
    flash->chip_info.memory_type = jedec_data[1];
    flash->chip_info.capacity = jedec_data[2];

    // Buscar chip en la tabla
    for (int i = 0; flash_chips[i].total_size != 0; i++) {
        if (flash_chips[i].manufacturer_id == jedec_data[0] &&
            flash_chips[i].memory_type == jedec_data[1] &&
            flash_chips[i].capacity == jedec_data[2]) {

            flash->chip_info.total_size = flash_chips[i].total_size;
            strncpy(flash->chip_info.chip_name, flash_chips[i].name,
                    sizeof(flash->chip_info.chip_name) - 1);
            return true;
        }
    }

    // Chip no reconocido, calcular tamaño por capacidad
    if (jedec_data[2] >= 0x14 && jedec_data[2] <= 0x20) {
        flash->chip_info.total_size = 1 << jedec_data[2]; // 2^capacity
        strncpy(flash->chip_info.chip_name, "Unknown",
                sizeof(flash->chip_info.chip_name) - 1);
        return true;
    }

    return false;
}

bool SPIFlash_IsReady(SPIFlash_t *flash) {
    if (!flash) return false;
    return !(SPIFlash_ReadStatus(flash) & SPIFLASH_STATUS_BUSY);
}

bool SPIFlash_WriteEnable(SPIFlash_t *flash) {
    if (!flash || flash->write_protection_enabled) return false;
    return SPIFlash_SendCommand(flash, SPIFLASH_CMD_WRITE_ENABLE);
}

bool SPIFlash_WriteDisable(SPIFlash_t *flash) {
    if (!flash) return false;
    return SPIFlash_SendCommand(flash, SPIFLASH_CMD_WRITE_DISABLE);
}

uint8_t SPIFlash_ReadStatus(SPIFlash_t *flash) {
    if (!flash || !flash->hspi) return 0xFF;

    uint8_t cmd = SPIFLASH_CMD_READ_STATUS;
    uint8_t status = 0;

    SPIFLASH_CS_LOW(flash);
    HAL_SPI_Transmit(flash->hspi, &cmd, 1, SPIFLASH_TIMEOUT_MS);
    HAL_SPI_Receive(flash->hspi, &status, 1, SPIFLASH_TIMEOUT_MS);
    SPIFLASH_CS_HIGH(flash);

    return status;
}

bool SPIFlash_WaitForReady(SPIFlash_t *flash, uint32_t timeout_ms) {
    if (!flash) return false;

    uint32_t start_time = HAL_GetTick();
    while ((HAL_GetTick() - start_time) < timeout_ms) {
        if (SPIFlash_IsReady(flash)) {
            return true;
        }
        HAL_Delay(1);
    }
    return false;
}

bool SPIFlash_ReadData(SPIFlash_t *flash, uint32_t address, uint8_t *data, uint32_t length) {
    if (!flash || !data || !flash->is_initialized) return false;
    if (!SPIFlash_IsAddressValid(flash, address + length - 1)) return false;

    uint8_t cmd_buffer[4] = {
        SPIFLASH_CMD_READ_DATA,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    };

    SPIFLASH_CS_LOW(flash);
    HAL_StatusTypeDef status1 = HAL_SPI_Transmit(flash->hspi, cmd_buffer, 4, SPIFLASH_TIMEOUT_MS);
    HAL_StatusTypeDef status2 = HAL_SPI_Receive(flash->hspi, data, length, SPIFLASH_TIMEOUT_MS);
    SPIFLASH_CS_HIGH(flash);

    return (status1 == HAL_OK && status2 == HAL_OK);
}

bool SPIFlash_FastRead(SPIFlash_t *flash, uint32_t address, uint8_t *data, uint32_t length) {
    if (!flash || !data || !flash->is_initialized) return false;
    if (!SPIFlash_IsAddressValid(flash, address + length - 1)) return false;

    uint8_t cmd_buffer[5] = {
        SPIFLASH_CMD_FAST_READ,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF,
        0x00  // Dummy byte
    };

    SPIFLASH_CS_LOW(flash);
    HAL_StatusTypeDef status1 = HAL_SPI_Transmit(flash->hspi, cmd_buffer, 5, SPIFLASH_TIMEOUT_MS);
    HAL_StatusTypeDef status2 = HAL_SPI_Receive(flash->hspi, data, length, SPIFLASH_TIMEOUT_MS);
    SPIFLASH_CS_HIGH(flash);

    return (status1 == HAL_OK && status2 == HAL_OK);
}

bool SPIFlash_WritePage(SPIFlash_t *flash, uint32_t address, const uint8_t *data, uint32_t length) {
    if (!flash || !data || !flash->is_initialized) return false;
    if (length > SPIFLASH_PAGE_SIZE) return false;
    if (!SPIFlash_IsAddressValid(flash, address + length - 1)) return false;

    if (!SPIFlash_WriteEnable(flash)) return false;
    if (!SPIFlash_WaitForReady(flash, SPIFLASH_TIMEOUT_MS)) return false;

    uint8_t cmd_buffer[4] = {
        SPIFLASH_CMD_PAGE_PROGRAM,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    };

    SPIFLASH_CS_LOW(flash);
    HAL_StatusTypeDef status1 = HAL_SPI_Transmit(flash->hspi, cmd_buffer, 4, SPIFLASH_TIMEOUT_MS);
    HAL_StatusTypeDef status2 = HAL_SPI_Transmit(flash->hspi, (uint8_t*)data, length, SPIFLASH_TIMEOUT_MS);
    SPIFLASH_CS_HIGH(flash);

    if (status1 != HAL_OK || status2 != HAL_OK) {
        return false;
    }

    return SPIFlash_WaitForReady(flash, SPIFLASH_TIMEOUT_MS);
}

bool SPIFlash_WriteData(SPIFlash_t *flash, uint32_t address, const uint8_t *data, uint32_t length) {
    if (!flash || !data || !flash->is_initialized) return false;

    uint32_t bytes_written = 0;
    while (bytes_written < length) {
        uint32_t page_address = address + bytes_written;
        uint32_t page_offset = page_address % SPIFLASH_PAGE_SIZE;
        uint32_t bytes_to_write = SPIFLASH_PAGE_SIZE - page_offset;

        if (bytes_to_write > (length - bytes_written)) {
            bytes_to_write = length - bytes_written;
        }

        if (!SPIFlash_WritePage(flash, page_address, data + bytes_written, bytes_to_write)) {
            return false;
        }

        bytes_written += bytes_to_write;
    }

    return true;
}

bool SPIFlash_EraseSector(SPIFlash_t *flash, uint32_t address) {
    if (!flash || !flash->is_initialized) return false;
    if (!SPIFlash_IsAddressValid(flash, address)) return false;

    if (!SPIFlash_WriteEnable(flash)) return false;
    if (!SPIFlash_WaitForReady(flash, SPIFLASH_TIMEOUT_MS)) return false;

    uint8_t cmd_buffer[4] = {
        SPIFLASH_CMD_SECTOR_ERASE,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    };

    if (!SPIFlash_Transaction(flash, cmd_buffer, NULL, 4)) {
        return false;
    }

    return SPIFlash_WaitForReady(flash, SPIFLASH_ERASE_TIMEOUT_MS);
}

bool SPIFlash_EraseChip(SPIFlash_t *flash) {
    if (!flash || !flash->is_initialized) return false;

    if (!SPIFlash_WriteEnable(flash)) return false;
    if (!SPIFlash_WaitForReady(flash, SPIFLASH_TIMEOUT_MS)) return false;

    if (!SPIFlash_SendCommand(flash, SPIFLASH_CMD_CHIP_ERASE)) {
        return false;
    }

    return SPIFlash_WaitForReady(flash, SPIFLASH_ERASE_TIMEOUT_MS * 10); // Chip erase toma más tiempo
}

uint8_t SPIFlash_ReadByte(SPIFlash_t *flash, uint32_t address) {
    uint8_t data = 0;
    SPIFlash_ReadData(flash, address, &data, 1);
    return data;
}

bool SPIFlash_WriteByte(SPIFlash_t *flash, uint32_t address, uint8_t data) {
    return SPIFlash_WriteData(flash, address, &data, 1);
}

bool SPIFlash_WriteString(SPIFlash_t *flash, uint32_t address, const char *str) {
    if (!str) return false;
    uint32_t length = strlen(str) + 1; // Incluir terminador nulo
    return SPIFlash_WriteData(flash, address, (const uint8_t*)str, length);
}

bool SPIFlash_ReadString(SPIFlash_t *flash, uint32_t address, char *str, uint32_t max_length) {
    if (!str || max_length == 0) return false;

    if (!SPIFlash_ReadData(flash, address, (uint8_t*)str, max_length)) {
        return false;
    }

    str[max_length - 1] = '\0'; // Asegurar terminación
    return true;
}

bool SPIFlash_WriteStruct(SPIFlash_t *flash, uint32_t address, const void *data, uint32_t size) {
    if (!data) return false;
    return SPIFlash_WriteData(flash, address, (const uint8_t*)data, size);
}

bool SPIFlash_ReadStruct(SPIFlash_t *flash, uint32_t address, void *data, uint32_t size) {
    if (!data) return false;
    return SPIFlash_ReadData(flash, address, (uint8_t*)data, size);
}

uint32_t SPIFlash_GetSectorAddress(uint32_t address) {
    return address & ~(SPIFLASH_SECTOR_SIZE - 1);
}

bool SPIFlash_IsAddressValid(SPIFlash_t *flash, uint32_t address) {
    if (!flash || !flash->is_initialized) return false;
    return (address < flash->chip_info.total_size);
}

void SPIFlash_GetChipInfoString(SPIFlash_t *flash, char *buffer, size_t buffer_size) {
    if (!flash || !buffer) return;

    snprintf(buffer, buffer_size,
             "Chip: %s, ID: 0x%02X%02X%02X, Size: %lu bytes",
             flash->chip_info.chip_name,
             flash->chip_info.manufacturer_id,
             flash->chip_info.memory_type,
             flash->chip_info.capacity,
             flash->chip_info.total_size);
}

uint32_t SPIFlash_GetTotalSize(SPIFlash_t *flash) {
    if (!flash || !flash->is_initialized) return 0;
    return flash->chip_info.total_size;
}

bool SPIFlash_PowerDown(SPIFlash_t *flash) {
    if (!flash) return false;
    return SPIFlash_SendCommand(flash, SPIFLASH_CMD_POWER_DOWN);
}

bool SPIFlash_PowerUp(SPIFlash_t *flash) {
    if (!flash) return false;
    bool result = SPIFlash_SendCommand(flash, SPIFLASH_CMD_POWER_UP);
    HAL_Delay(10); // Esperar wake-up
    return result;
}

// Funciones específicas para W25Q128JVS
bool SPIFlash_IsW25Q128(SPIFlash_t *flash) {
    if (!flash || !flash->is_initialized) return false;

    return (flash->chip_info.manufacturer_id == 0xEF &&
            flash->chip_info.memory_type == 0x40 &&
            flash->chip_info.capacity == 0x18);
}

uint32_t SPIFlash_GetSectorCount(SPIFlash_t *flash) {
    if (!flash || !flash->is_initialized) return 0;

    if (SPIFlash_IsW25Q128(flash)) {
        return SPIFLASH_TOTAL_SECTORS; // 4096 sectores para W25Q128
    }

    return flash->chip_info.total_size / SPIFLASH_SECTOR_SIZE;
}

uint32_t SPIFlash_GetPageCount(SPIFlash_t *flash) {
    if (!flash || !flash->is_initialized) return 0;

    if (SPIFlash_IsW25Q128(flash)) {
        return SPIFLASH_TOTAL_PAGES; // 65536 páginas para W25Q128
    }

    return flash->chip_info.total_size / SPIFLASH_PAGE_SIZE;
}

void SPIFlash_GetMemoryMap(SPIFlash_t *flash, char *buffer, size_t buffer_size) {
    if (!flash || !buffer) return;

    if (SPIFlash_IsW25Q128(flash)) {
        snprintf(buffer, buffer_size,
                 "W25Q128JVS Memory Map:\n"
                 "Total: 16MB (128Mbit)\n"
                 "Pages: 65536 x 256 bytes\n"
                 "Sectors: 4096 x 4KB\n"
                 "Blocks 32K: 512 x 32KB\n"
                 "Blocks 64K: 256 x 64KB\n"
                 "Address: 0x000000 - 0xFFFFFF");
    } else {
        snprintf(buffer, buffer_size,
                 "Flash Memory Map:\n"
                 "Total: %lu bytes\n"
                 "Pages: %lu x %d bytes\n"
                 "Sectors: %lu x %d bytes",
                 flash->chip_info.total_size,
                 SPIFlash_GetPageCount(flash), SPIFLASH_PAGE_SIZE,
                 SPIFlash_GetSectorCount(flash), SPIFLASH_SECTOR_SIZE);
    }
}