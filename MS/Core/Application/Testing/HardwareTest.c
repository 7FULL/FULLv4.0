/**
 ******************************************************************************
 * @file           : HardwareTest.c
 * @brief          : Hardware test suite implementation
 ******************************************************************************
 */

#include "HardwareTest.h"
#include <stdio.h>
#include <string.h>

// Test configuration
#define TEST_DELAY_MS           2000
#define SENSOR_READ_SAMPLES     10
#define GPS_FIX_TIMEOUT_MS      300000  // 5 minutes for GPS fix

// Color definitions for LED test
#define COLOR_RED      {255, 0, 0}
#define COLOR_GREEN    {0, 255, 0}
#define COLOR_BLUE     {0, 0, 255}
#define COLOR_YELLOW   {255, 255, 0}
#define COLOR_CYAN     {0, 255, 255}
#define COLOR_MAGENTA  {255, 0, 255}
#define COLOR_WHITE    {255, 255, 255}
#define COLOR_OFF      {0, 0, 0}

// Helper function to log messages
static void LogMessage(HardwareTest_t* test, const char* msg) {
    if (test->hardware.sdlogger != NULL) {
        SDLogger_WriteText(test->hardware.sdlogger, msg);
    }
}

/**
 * @brief Initialize hardware test system with default configuration
 */
bool HardwareTest_Init(HardwareTest_t* test) {
    memset(test, 0, sizeof(HardwareTest_t));

    // Set default configuration values
    test->config.accelerometer_range = 0;      // Default: ±8g for ground testing
    test->config.gps_timeout_ms = GPS_FIX_TIMEOUT_MS;  // Default: 5 minutes

    test->test_start_time = HAL_GetTick();
    test->current_test = 0;
    return true;
}

/**
 * @brief Load configuration from SD card rocket_config.txt
 */
bool HardwareTest_LoadConfig(HardwareTest_t* test) {
    if (test->hardware.sdlogger == NULL) {
        LogMessage(test, "WARNING: Cannot load config - SD logger not initialized");
        return false;
    }

    LogMessage(test, "");
    LogMessage(test, "=== LOADING CONFIGURATION ===");
    LogMessage(test, "Reading rocket_config.txt from SD card...");

    FIL file;
    FRESULT fr = f_open(&file, "rocket_config.txt", FA_READ);

    if (fr != FR_OK) {
        LogMessage(test, "WARNING: rocket_config.txt not found - using defaults");
        LogMessage(test, "  You can create rocket_config.txt on SD card to customize settings");
        return false;
    }

    char line[128];
    int configs_loaded = 0;

    while (f_gets(line, sizeof(line), &file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        // Parse ACCELEROMETER_RANGE
        if (strncmp(line, "ACCELEROMETER_RANGE=", 20) == 0) {
            int range = atoi(line + 20);
            if (range >= 0 && range <= 3) {
                test->config.accelerometer_range = (uint8_t)range;
                char msg[80];
                const char* range_str[] = {"±8g", "±16g", "±32g", "±64g"};
                sprintf(msg, "  Accelerometer range: %s", range_str[range]);
                LogMessage(test, msg);
                configs_loaded++;
            }
        }

        // Parse GPS_TIMEOUT_SECONDS
        if (strncmp(line, "GPS_TIMEOUT_SECONDS=", 20) == 0) {
            int timeout_s = atoi(line + 20);
            if (timeout_s > 0 && timeout_s <= 600) { // Max 10 minutes
                test->config.gps_timeout_ms = (uint32_t)timeout_s * 1000;
                char msg[80];
                sprintf(msg, "  GPS timeout: %d seconds", timeout_s);
                LogMessage(test, msg);
                configs_loaded++;
            }
        }
    }

    f_close(&file);

    char msg[80];
    sprintf(msg, "Configuration loaded: %d parameters", configs_loaded);
    LogMessage(test, msg);
    LogMessage(test, "");

    return true;
}

/**
 * @brief Test KX134 accelerometer
 */
bool HardwareTest_TestKX134(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING KX134 ACCELEROMETER ===");

    KX134_t* kx134 = test->hardware.kx134;

    // Check if sensor is initialized
    LogMessage(test, "Initializing KX134 on SPI1, CS=PB1...");
    if (!KX134_Init(kx134, &hspi1, GPIOB, GPIO_PIN_1)) {
        LogMessage(test, "ERROR: KX134 initialization failed");
        LogMessage(test, "  - Check SPI1 connections (MOSI, MISO, SCK)");
        LogMessage(test, "  - Check CS pin PB1");
        LogMessage(test, "  - Check power supply (3.3V)");
        LogMessage(test, "  - Check WHO_AM_I register response");
        test->results.kx134_ok = false;
        return false;
    }

    LogMessage(test, "SUCCESS: KX134 initialized correctly");

    // Configure sensor range from config
    const char* range_str[] = {"±8g", "±16g", "±32g", "±64g"};
    char config_msg[80];
    sprintf(config_msg, "Configuring KX134 range (%s)...", range_str[test->config.accelerometer_range]);
    LogMessage(test, config_msg);

    if (!KX134_Configure(kx134, test->config.accelerometer_range)) {
        LogMessage(test, "ERROR: KX134 configuration failed");
        test->results.kx134_ok = false;
        return false;
    }
    LogMessage(test, "SUCCESS: KX134 configured");

    // Enable sensor
    LogMessage(test, "Enabling KX134 sensor...");
    if (!KX134_Enable(kx134)) {
        LogMessage(test, "ERROR: KX134 enable failed");
        test->results.kx134_ok = false;
        return false;
    }
    LogMessage(test, "SUCCESS: KX134 enabled and ready");

    // Wait for sensor to stabilize
    HAL_Delay(100);

    // Read multiple samples
    LogMessage(test, "Reading acceleration data (10 samples)...");
    float acc_x_sum = 0, acc_y_sum = 0, acc_z_sum = 0;
    KX134_AccelData_t accel_data;

    for (int i = 0; i < SENSOR_READ_SAMPLES; i++) {
        KX134_ReadAccelG(kx134, &accel_data);
        acc_x_sum += accel_data.x;
        acc_y_sum += accel_data.y;
        acc_z_sum += accel_data.z;
        HAL_Delay(50);
    }

    float acc_x_avg = acc_x_sum / SENSOR_READ_SAMPLES;
    float acc_y_avg = acc_y_sum / SENSOR_READ_SAMPLES;
    float acc_z_avg = acc_z_sum / SENSOR_READ_SAMPLES;

    // Log results
    char msg[150];
    sprintf(msg, "Average acceleration (G):");
    LogMessage(test, msg);
    sprintf(msg, "  X: %d.%03d", (int)acc_x_avg, abs((int)(acc_x_avg * 1000) % 1000));
    LogMessage(test, msg);
    sprintf(msg, "  Y: %d.%03d", (int)acc_y_avg, abs((int)(acc_y_avg * 1000) % 1000));
    LogMessage(test, msg);
    sprintf(msg, "  Z: %d.%03d", (int)acc_z_avg, abs((int)(acc_z_avg * 1000) % 1000));
    LogMessage(test, msg);

    // Sanity check - Z axis should be close to 1G when stationary
    if (acc_z_avg > 0.5f && acc_z_avg < 1.5f) {
        LogMessage(test, "PASS: Z-axis reading is reasonable (0.5-1.5G)");
        test->results.kx134_ok = true;
    } else {
        LogMessage(test, "WARNING: Z-axis reading outside expected range");
        LogMessage(test, "  Expected ~1.0G when stationary, check sensor orientation");
        test->results.kx134_ok = false;
    }

    return test->results.kx134_ok;
}

/**
 * @brief Test MS5611 barometer
 */
bool HardwareTest_TestMS5611(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING MS5611 BAROMETER ===");

    MS5611_t* ms5611 = test->hardware.ms5611;

    // Initialize sensor
    LogMessage(test, "Initializing MS5611 on SPI1, CS=PC4...");
    if (!MS5611_Init(ms5611, &hspi1, GPIOC, GPIO_PIN_4)) {
        LogMessage(test, "ERROR: MS5611 initialization failed");
        LogMessage(test, "  - Check SPI1 connections");
        LogMessage(test, "  - Check CS pin PC4");
        LogMessage(test, "  - Check power supply (3.3V)");
        LogMessage(test, "  - Check PROM calibration data");
        test->results.ms5611_ok = false;
        return false;
    }

    LogMessage(test, "SUCCESS: MS5611 initialized correctly");

    // Read multiple samples
    LogMessage(test, "Reading pressure/temperature data (10 samples)...");
    float pressure_sum = 0, temp_sum = 0, alt_sum = 0;
    MS5611_Data_t ms_data;

    for (int i = 0; i < SENSOR_READ_SAMPLES; i++) {
        MS5611_ReadData(ms5611, &ms_data);

        pressure_sum += ms_data.pressure;
        temp_sum += ms_data.temperature;
        alt_sum += (float)ms_data.altitude;
        HAL_Delay(100);
    }

    float pressure_avg = pressure_sum / SENSOR_READ_SAMPLES;
    float temp_avg = temp_sum / SENSOR_READ_SAMPLES;
    float alt_avg = alt_sum / SENSOR_READ_SAMPLES;

    // Log results
    char msg[150];
    sprintf(msg, "Average readings:");
    LogMessage(test, msg);
    sprintf(msg, "  Pressure: %d.%02d mbar",
            (int)pressure_avg, abs((int)(pressure_avg * 100) % 100));
    LogMessage(test, msg);
    sprintf(msg, "  Temperature: %d.%02d C",
            (int)temp_avg, abs((int)(temp_avg * 100) % 100));
    LogMessage(test, msg);
    sprintf(msg, "  Altitude: %d.%02d m",
            (int)alt_avg, abs((int)(alt_avg * 100) % 100));
    LogMessage(test, msg);

    // Sanity checks
    bool pressure_ok = (pressure_avg > 800.0f && pressure_avg < 1100.0f);
    bool temp_ok = (temp_avg > -20.0f && temp_avg < 50.0f);

    if (pressure_ok && temp_ok) {
        LogMessage(test, "PASS: Pressure and temperature within reasonable range");
        test->results.ms5611_ok = true;
    } else {
        LogMessage(test, "WARNING: Readings outside expected range");
        if (!pressure_ok) LogMessage(test, "  Pressure should be 800-1100 mbar at sea level");
        if (!temp_ok) LogMessage(test, "  Temperature should be -20 to 50C");
        test->results.ms5611_ok = false;
    }

    return test->results.ms5611_ok;
}

/**
 * @brief Test ZOE-M8Q GPS module
 */
bool HardwareTest_TestGPS(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING ZOE-M8Q GPS ===");

    ZOE_M8Q_t* gps = test->hardware.gps;

    // Initialize GPS
    LogMessage(test, "Initializing GPS on I2C3, address 0x42...");
    if (!ZOE_M8Q_Init(gps, &hi2c3)) {
        LogMessage(test, "ERROR: GPS initialization failed");
        LogMessage(test, "  - Check I2C3 connections (SCL, SDA)");
        LogMessage(test, "  - Check I2C address 0x42");
        LogMessage(test, "  - Check power supply");
        LogMessage(test, "  - Check antenna connection");
        test->results.gps_ok = false;
        return false;
    }

    LogMessage(test, "SUCCESS: GPS initialized correctly");

    char timeout_msg[80];
    sprintf(timeout_msg, "Waiting for GPS fix (timeout %lu seconds)...", test->config.gps_timeout_ms / 1000);
    LogMessage(test, timeout_msg);
    LogMessage(test, "NOTE: GPS needs clear sky view. This may take several minutes.");

    // Try to get a fix
    uint32_t gps_start = HAL_GetTick();
    bool got_fix = false;
    uint32_t last_log_time = 0;

    while ((HAL_GetTick() - gps_start) < test->config.gps_timeout_ms) {
        ZOE_M8Q_ReadData(gps);

        if (ZOE_M8Q_HasValidFix(gps)) {
            got_fix = true;
            break;
        }

        // Log progress every 30 seconds
        uint32_t elapsed = (HAL_GetTick() - gps_start) / 1000;
        if (elapsed - last_log_time >= 30) {
            char msg[80];
            sprintf(msg, "  Still waiting for GPS fix... (%lu seconds elapsed)", elapsed);
            LogMessage(test, msg);
            last_log_time = elapsed;
        }

        // Visual feedback - blink LED
        if (test->hardware.led != NULL) {
            WS2812B_SetColorRGB(test->hardware.led, 255, 255, 0); // Yellow = waiting
            HAL_Delay(100);
            WS2812B_SetColorRGB(test->hardware.led, 0, 0, 0);
            HAL_Delay(900);
        } else {
            HAL_Delay(1000);
        }
    }

    if (got_fix) {
        char msg[150];
        LogMessage(test, "SUCCESS: GPS fix acquired!");
        sprintf(msg, "  Latitude: %d.%06d",
                (int)gps->gps_data.latitude, abs((int)(gps->gps_data.latitude * 1000000) % 1000000));
        LogMessage(test, msg);
        sprintf(msg, "  Longitude: %d.%06d",
                (int)gps->gps_data.longitude, abs((int)(gps->gps_data.longitude * 1000000) % 1000000));
        LogMessage(test, msg);
        sprintf(msg, "  Altitude: %d.%02d m",
                (int)gps->gps_data.altitude, abs((int)(gps->gps_data.altitude * 100) % 100));
        LogMessage(test, msg);
        sprintf(msg, "  Satellites: %d", gps->gps_data.satellites_used);
        LogMessage(test, msg);
        sprintf(msg, "  Fix type: %d", gps->gps_data.fix_type);
        LogMessage(test, msg);

        test->results.gps_ok = true;
    } else {
        LogMessage(test, "WARNING: GPS fix not acquired within timeout");
        LogMessage(test, "  - GPS may need more time to acquire satellites");
        LogMessage(test, "  - Check antenna placement (needs clear sky view)");
        LogMessage(test, "  - GPS is not critical for initial hardware test");
        test->results.gps_ok = false;
    }

    return test->results.gps_ok;
}

/**
 * @brief Test W25Q128 SPI Flash memory
 */
bool HardwareTest_TestFlash(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING W25Q128 SPI FLASH ===");

    SPIFlash_t* flash = test->hardware.flash;

    // Initialize flash
    LogMessage(test, "Initializing SPI Flash on SPI1, CS=PC15...");
    if (!SPIFlash_Init(flash, &hspi1)) {
        LogMessage(test, "ERROR: SPI Flash initialization failed");
        LogMessage(test, "  - Check SPI1 connections");
        LogMessage(test, "  - Check CS pin PC15");
        LogMessage(test, "  - Check WP and HOLD pins (pull high)");
        LogMessage(test, "  - Check power supply");
        test->results.flash_ok = false;
        return false;
    }

    LogMessage(test, "SUCCESS: SPI Flash initialized correctly");

    // Test write/read cycle
    LogMessage(test, "Testing write/read cycle...");

    uint8_t test_data[256];
    uint8_t read_data[256];

    // Fill test data with pattern
    for (int i = 0; i < 256; i++) {
        test_data[i] = (uint8_t)i;
    }

    // Erase first sector (4KB)
    LogMessage(test, "Erasing test sector at address 0x000000...");
    SPIFlash_EraseSector(flash, 0x000000);

    // Write test data
    LogMessage(test, "Writing 256 bytes of test data...");
    SPIFlash_WritePage(flash, 0x000000, test_data, 256);

    // Read back
    LogMessage(test, "Reading back 256 bytes...");
    SPIFlash_ReadData(flash, 0x000000, read_data, 256);

    // Verify
    bool verify_ok = true;
    for (int i = 0; i < 256; i++) {
        if (test_data[i] != read_data[i]) {
            verify_ok = false;
            break;
        }
    }

    if (verify_ok) {
        LogMessage(test, "PASS: Write/read verification successful");
        LogMessage(test, "  All 256 bytes match expected pattern");
        test->results.flash_ok = true;
    } else {
        LogMessage(test, "ERROR: Write/read verification failed");
        LogMessage(test, "  Data mismatch detected");
        test->results.flash_ok = false;
    }

    return test->results.flash_ok;
}

/**
 * @brief Test SD Card
 */
bool HardwareTest_TestSD(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING SD CARD ===");

    SDLogger_t* sdlogger = test->hardware.sdlogger;

    // Initialize SD card
    LogMessage(test, "Initializing SD Card via FATFS...");
    if (!SDLogger_Init(sdlogger)) {
        LogMessage(test, "ERROR: SD Card initialization failed");
        LogMessage(test, "  - Check SD card is inserted");
        LogMessage(test, "  - Check SD card format (FAT32)");
        LogMessage(test, "  - Check SPI connections");
        LogMessage(test, "  - Try different SD card");
        test->results.sd_ok = false;
        return false;
    }

    LogMessage(test, "SUCCESS: SD Card initialized correctly");

    // Test file write
    LogMessage(test, "Testing file write operation...");
    SDLogger_CreateDebugFile(sdlogger);
    SDLogger_WriteText(sdlogger, "=== SD CARD WRITE TEST ===");
    SDLogger_WriteText(sdlogger, "This is a test message");
    SDLogger_WriteText(sdlogger, "If you can read this, SD write works!");

    LogMessage(test, "PASS: SD Card write test successful");
    LogMessage(test, "  Check debug.txt file on SD card");

    test->results.sd_ok = true;
    return true;
}

/**
 * @brief Test WS2812B RGB LED
 */
bool HardwareTest_TestLED(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING WS2812B RGB LED ===");

    WS2812B_t* led = test->hardware.led;

    // Initialize LED
    LogMessage(test, "Initializing WS2812B on TIM1_CH2 (PA9)...");
    if (!WS2812B_Init(led, &htim1, TIM_CHANNEL_2)) {
        LogMessage(test, "ERROR: WS2812B initialization failed");
        LogMessage(test, "  - Check PWM timer TIM1 configuration");
        LogMessage(test, "  - Check DMA configuration");
        LogMessage(test, "  - Check data pin PA9");
        LogMessage(test, "  - Check LED power supply (5V)");
        test->results.led_ok = false;
        return false;
    }

    LogMessage(test, "SUCCESS: WS2812B initialized correctly");
    LogMessage(test, "Testing color sequence (watch the LED)...");

    // Test color sequence
    uint8_t colors[][3] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE,
                           COLOR_YELLOW, COLOR_CYAN, COLOR_MAGENTA,
                           COLOR_WHITE, COLOR_OFF};
    const char* color_names[] = {"RED", "GREEN", "BLUE", "YELLOW",
                                  "CYAN", "MAGENTA", "WHITE", "OFF"};

    for (int i = 0; i < 8; i++) {
        char msg[50];
        sprintf(msg, "  Color: %s", color_names[i]);
        LogMessage(test, msg);
        WS2812B_SetColorRGB(led, colors[i][0], colors[i][1], colors[i][2]);
        HAL_Delay(500);
    }

    LogMessage(test, "PASS: LED color test complete");
    LogMessage(test, "  If all colors displayed correctly, LED is working");

    test->results.led_ok = true;
    return true;
}

/**
 * @brief Test Buzzer
 */
bool HardwareTest_TestBuzzer(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING BUZZER ===");

    Buzzer_t* buzzer = test->hardware.buzzer;

    // Initialize buzzer
    LogMessage(test, "Initializing Buzzer on PB12...");
    if (!Buzzer_Init(buzzer)) {
        LogMessage(test, "ERROR: Buzzer initialization failed");
        LogMessage(test, "  - Check GPIO pin PB12");
        LogMessage(test, "  - Check buzzer connection");
        LogMessage(test, "  - Check power supply");
        test->results.buzzer_ok = false;
        return false;
    }

    LogMessage(test, "SUCCESS: Buzzer initialized correctly");
    LogMessage(test, "Testing buzzer patterns (listen for sounds)...");

    // Test patterns
    LogMessage(test, "  Pattern: INIT (short beep)");
    Buzzer_Pattern(buzzer, BUZZER_PATTERN_INIT);
    HAL_Delay(1000);

    LogMessage(test, "  Pattern: SUCCESS (rising tone)");
    Buzzer_Pattern(buzzer, BUZZER_PATTERN_SUCCESS);
    HAL_Delay(1500);

    LogMessage(test, "  Pattern: ERROR (descending tone)");
    Buzzer_Pattern(buzzer, BUZZER_PATTERN_ERROR);
    HAL_Delay(1500);

    LogMessage(test, "  Pattern: WARNING (alternating beeps)");
    Buzzer_Pattern(buzzer, BUZZER_PATTERN_WARNING);
    HAL_Delay(2000);

    LogMessage(test, "PASS: Buzzer pattern test complete");
    LogMessage(test, "  If all patterns played correctly, buzzer is working");

    test->results.buzzer_ok = true;
    return true;
}

/**
 * @brief Test servo motors
 */
bool HardwareTest_TestServos(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING SERVO MOTORS ===");

    ServoControl_t* servo = test->hardware.servo;

    // Initialize servos
    LogMessage(test, "Initializing 4 servo channels...");
    LogMessage(test, "  Servo 1: PB8 (TIM4)");
    LogMessage(test, "  Servo 2: PA3 (TIM2)");
    LogMessage(test, "  Servo 3: PA2 (TIM2)");
    LogMessage(test, "  Servo 4: PA1 (TIM2)");

    if (!ServoControl_Init(servo)) {
        LogMessage(test, "ERROR: Servo initialization failed");
        LogMessage(test, "  - Check PWM timer configuration");
        LogMessage(test, "  - Check GPIO alternate functions");
        test->results.servo_ok = false;
        return false;
    }

    // Set timers after initialization
    if (!ServoControl_SetTimers(servo, &htim4, &htim2)) {
        LogMessage(test, "ERROR: Failed to set servo timers");
        test->results.servo_ok = false;
        return false;
    }

    LogMessage(test, "SUCCESS: Servos initialized correctly");

    // Test sweep for each servo
    LogMessage(test, "Testing servo movement (0 to 180 degrees)...");
    LogMessage(test, "WARNING: Ensure servos are mechanically free to move!");
    HAL_Delay(2000);

    for (int servo_id = 0; servo_id < 4; servo_id++) {
        char msg[50];
        sprintf(msg, "  Testing Servo %d:", servo_id + 1);
        LogMessage(test, msg);

        // Enable servo
        ServoControl_EnableServo(servo, servo_id);

        // Move to 0 degrees
        LogMessage(test, "    Position: 0 deg");
        ServoControl_SetAngle(servo, servo_id, 0);
        HAL_Delay(1000);

        // Move to 90 degrees
        LogMessage(test, "    Position: 90 deg");
        ServoControl_SetAngle(servo, servo_id, 90);
        HAL_Delay(1000);

        // Move to 180 degrees
        LogMessage(test, "    Position: 180 deg");
        ServoControl_SetAngle(servo, servo_id, 180);
        HAL_Delay(1000);

        // Return to 90 degrees (neutral)
        LogMessage(test, "    Position: 90 deg (neutral)");
        ServoControl_SetAngle(servo, servo_id, 90);
        HAL_Delay(500);

        // Disable servo
        ServoControl_DisableServo(servo, servo_id);
    }

    LogMessage(test, "PASS: Servo test complete");
    LogMessage(test, "  Verify all servos moved smoothly through full range");

    test->results.servo_ok = true;
    return true;
}

/**
 * @brief Test pyrotechnic channels (WARNING: DO NOT CONNECT PYRO CHARGES!)
 */
bool HardwareTest_TestPyroChannels(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== TESTING PYROTECHNIC CHANNELS ===");
    LogMessage(test, "WARNING: DO NOT CONNECT ACTUAL PYRO CHARGES!");
    LogMessage(test, "WARNING: This test only toggles GPIOs for verification");
    LogMessage(test, "");
    HAL_Delay(2000);

    // Initialize pyro channels
    LogMessage(test, "Initializing 4 pyro channels...");
    LogMessage(test, "  Channel 1: PC3");
    LogMessage(test, "  Channel 2: PC2");
    LogMessage(test, "  Channel 3: PC1");
    LogMessage(test, "  Channel 4: PB9");

    PyroChannels_Init();
    LogMessage(test, "SUCCESS: Pyro channels initialized (all OFF)");

    // Test each channel
    LogMessage(test, "Testing GPIO toggle on each channel...");
    LogMessage(test, "  Measure voltage on MOSFETs with multimeter");
    LogMessage(test, "");

    for (int ch = 0; ch < 4; ch++) {
        char msg[50];
        sprintf(msg, "  Channel %d: Activating for 500ms...", ch + 1);
        LogMessage(test, msg);

        PyroChannels_ActivateChannel(ch);
        HAL_Delay(500);
        PyroChannels_DeactivateChannel(ch);
        HAL_Delay(500);
    }

    LogMessage(test, "");
    LogMessage(test, "PASS: Pyro channel test complete");
    LogMessage(test, "  Verify all channels toggled correctly");
    LogMessage(test, "  Expected: 3.3V when active, 0V when inactive");

    test->results.pyro_ok = true;
    return true;
}

/**
 * @brief Run all hardware tests sequentially
 */
void HardwareTest_RunAll(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "╔═══════════════════════════════════════════════════════╗");
    LogMessage(test, "║   MASTER MCU HARDWARE TEST SUITE                      ║");
    LogMessage(test, "║   STM32F411RET6 Flight Computer                       ║");
    LogMessage(test, "╚═══════════════════════════════════════════════════════╝");
    LogMessage(test, "");

    test->test_start_time = HAL_GetTick();

    // Run each test
    LogMessage(test, "Starting sequential hardware tests...");
    LogMessage(test, "");

    // 1. SD Card (needed for logging)
    test->current_test = 1;
    HardwareTest_TestSD(test);
    HAL_Delay(TEST_DELAY_MS);

    // Load configuration from SD card (after SD is initialized)
    HardwareTest_LoadConfig(test);

    // 2. LED (needed for visual feedback)
    test->current_test = 2;
    HardwareTest_TestLED(test);
    HAL_Delay(TEST_DELAY_MS);

    // 3. Buzzer (needed for audio feedback)
    test->current_test = 3;
    HardwareTest_TestBuzzer(test);
    HAL_Delay(TEST_DELAY_MS);

    // 4. SPI Flash
    test->current_test = 4;
    HardwareTest_TestFlash(test);
    HAL_Delay(TEST_DELAY_MS);

    // 5. Accelerometer
    test->current_test = 5;
    HardwareTest_TestKX134(test);
    HAL_Delay(TEST_DELAY_MS);

    // 6. Barometer
    test->current_test = 6;
    HardwareTest_TestMS5611(test);
    HAL_Delay(TEST_DELAY_MS);

    // 7. Servos
    test->current_test = 8;
    HardwareTest_TestServos(test);
    HAL_Delay(TEST_DELAY_MS);

    // 8. GPS
    test->current_test = 7;
    HardwareTest_TestGPS(test);
    HAL_Delay(TEST_DELAY_MS);

    // 9. Pyro Channels
    test->current_test = 9;
    HardwareTest_TestPyroChannels(test);
    HAL_Delay(TEST_DELAY_MS);

    // Print summary
    HardwareTest_PrintSummary(test);
}

/**
 * @brief Print test summary
 */
void HardwareTest_PrintSummary(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "╔═══════════════════════════════════════════════════════╗");
    LogMessage(test, "║   HARDWARE TEST SUMMARY                               ║");
    LogMessage(test, "╚═══════════════════════════════════════════════════════╝");
    LogMessage(test, "");

    uint32_t test_duration = (HAL_GetTick() - test->test_start_time) / 1000;
    char msg[100];

    sprintf(msg, "Total test duration: %ld seconds", test_duration);
    LogMessage(test, msg);
    LogMessage(test, "");

    // Component results
    LogMessage(test, "Component Test Results:");
    LogMessage(test, test->results.sd_ok         ? "  [PASS] SD Card" : "  [FAIL] SD Card");
    LogMessage(test, test->results.led_ok        ? "  [PASS] WS2812B LED" : "  [FAIL] WS2812B LED");
    LogMessage(test, test->results.buzzer_ok     ? "  [PASS] Buzzer" : "  [FAIL] Buzzer");
    LogMessage(test, test->results.flash_ok      ? "  [PASS] SPI Flash W25Q128" : "  [FAIL] SPI Flash W25Q128");
    LogMessage(test, test->results.kx134_ok      ? "  [PASS] KX134 Accelerometer" : "  [FAIL] KX134 Accelerometer");
    LogMessage(test, test->results.ms5611_ok     ? "  [PASS] MS5611 Barometer" : "  [FAIL] MS5611 Barometer");
    LogMessage(test, test->results.gps_ok        ? "  [PASS] ZOE-M8Q GPS" : "  [WARN] ZOE-M8Q GPS (not critical)");
    LogMessage(test, test->results.servo_ok      ? "  [PASS] Servo Motors (x4)" : "  [FAIL] Servo Motors (x4)");
    LogMessage(test, test->results.pyro_ok       ? "  [PASS] Pyro Channels (x4)" : "  [FAIL] Pyro Channels (x4)");

    LogMessage(test, "");

    // Overall verdict
    bool critical_ok = test->results.sd_ok && test->results.flash_ok &&
                       test->results.kx134_ok && test->results.ms5611_ok;

    if (critical_ok) {
        LogMessage(test, "╔═══════════════════════════════════════════════════════╗");
        LogMessage(test, "║   VERDICT: CRITICAL SYSTEMS OPERATIONAL               ║");
        LogMessage(test, "║   Master MCU ready for flight software integration    ║");
        LogMessage(test, "╚═══════════════════════════════════════════════════════╝");

        if (test->hardware.led != NULL) {
            WS2812B_SetColorRGB(test->hardware.led, 0, 255, 0); // Green
        }
        if (test->hardware.buzzer != NULL) {
            Buzzer_Pattern(test->hardware.buzzer, BUZZER_PATTERN_SUCCESS);
        }
    } else {
        LogMessage(test, "╔═══════════════════════════════════════════════════════╗");
        LogMessage(test, "║   VERDICT: CRITICAL FAILURES DETECTED                 ║");
        LogMessage(test, "║   Fix errors above before proceeding                  ║");
        LogMessage(test, "╚═══════════════════════════════════════════════════════╝");

        if (test->hardware.led != NULL) {
            WS2812B_SetColorRGB(test->hardware.led, 255, 0, 0); // Red
        }
        if (test->hardware.buzzer != NULL) {
            Buzzer_Pattern(test->hardware.buzzer, BUZZER_PATTERN_ERROR);
        }
    }

    LogMessage(test, "");
}

/**
 * @brief Continuous sensor monitoring mode (for real-time verification)
 */
void HardwareTest_ContinuousMonitoring(HardwareTest_t* test) {
    LogMessage(test, "");
    LogMessage(test, "=== ENTERING CONTINUOUS MONITORING MODE ===");
    LogMessage(test, "Press reset to exit...");
    LogMessage(test, "");

    uint32_t last_update = 0;

    while (1) {
        if ((HAL_GetTick() - last_update) >= 1000) {
            last_update = HAL_GetTick();

            // Read all sensors
            KX134_AccelData_t accel_data;
            MS5611_Data_t ms_data;

            KX134_ReadAccelG(test->hardware.kx134, &accel_data);
            MS5611_ReadData(test->hardware.ms5611, &ms_data);
            ZOE_M8Q_ReadData(test->hardware.gps);

            // Format and log data
            char msg[200];
            ZOE_M8Q_t* gps = test->hardware.gps;

            sprintf(msg, "ACC[X:%d.%02d Y:%d.%02d Z:%d.%02d]G BAR[P:%d.%01d T:%d.%01d A:%d.%01d] GPS[Fix:%d Sat:%d]",
                    (int)accel_data.x, abs((int)(accel_data.x*100)%100),
                    (int)accel_data.y, abs((int)(accel_data.y*100)%100),
                    (int)accel_data.z, abs((int)(accel_data.z*100)%100),
                    (int)ms_data.pressure, abs((int)(ms_data.pressure*10)%10),
                    (int)ms_data.temperature, abs((int)(ms_data.temperature*10)%10),
                    (int)ms_data.altitude, abs((int)((float)ms_data.altitude*10)%10),
                    gps->gps_data.fix_type, gps->gps_data.satellites_used);

            LogMessage(test, msg);

            // Visual feedback - flash LED
            if (test->hardware.led != NULL) {
                WS2812B_SetColorRGB(test->hardware.led, 0, 255, 255); // Cyan
                HAL_Delay(50);
                WS2812B_SetColorRGB(test->hardware.led, 0, 0, 0);
            }
        }
    }
}
