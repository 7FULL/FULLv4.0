/**
 ******************************************************************************
 * @file           : HardwareTest.h
 * @brief          : Hardware test suite for MASTER MCU components
 * @description    : Individual test routines for all sensors and actuators
 ******************************************************************************
 */

#ifndef HARDWARETEST_H
#define HARDWARETEST_H

#include "main.h"
#include "KX134.h"
#include "MS5611.h"
#include "ZOE_M8Q.h"
#include "WS2812B.h"
#include "Buzzer.h"
#include "SPIFlash.h"
#include "PyroChannels.h"
#include "ServoControl.h"
#include "SDLogger.h"
#include <stdbool.h>

// Test configuration structure
typedef struct {
    uint8_t accelerometer_range;  // 0=±8g, 1=±16g, 2=±32g, 3=±64g
    uint32_t gps_timeout_ms;      // GPS fix timeout in milliseconds
} HardwareTestConfig_t;

// Test result structure
typedef struct {
    bool kx134_ok;
    bool ms5611_ok;
    bool gps_ok;
    bool flash_ok;
    bool sd_ok;
    bool led_ok;
    bool buzzer_ok;
    bool servo_ok;
    bool pyro_ok;
} HardwareTestResults_t;

// Hardware instance pointers
typedef struct {
    KX134_t*     kx134;
    MS5611_t*    ms5611;
    ZOE_M8Q_t*   gps;
    SPIFlash_t*  flash;
    WS2812B_t*   led;
    Buzzer_t*    buzzer;
    SDLogger_t*  sdlogger;
    ServoControl_t* servo;
} HardwareInstances_t;

// Main test control structure
typedef struct {
    HardwareInstances_t hardware;
    HardwareTestResults_t results;
    HardwareTestConfig_t config;
    uint32_t test_start_time;
    uint8_t current_test;
} HardwareTest_t;

// Initialization
bool HardwareTest_Init(HardwareTest_t* test);
bool HardwareTest_LoadConfig(HardwareTest_t* test);

// Individual component tests
bool HardwareTest_TestKX134(HardwareTest_t* test);
bool HardwareTest_TestMS5611(HardwareTest_t* test);
bool HardwareTest_TestGPS(HardwareTest_t* test);
bool HardwareTest_TestFlash(HardwareTest_t* test);
bool HardwareTest_TestSD(HardwareTest_t* test);
bool HardwareTest_TestLED(HardwareTest_t* test);
bool HardwareTest_TestBuzzer(HardwareTest_t* test);
bool HardwareTest_TestServos(HardwareTest_t* test);
bool HardwareTest_TestPyroChannels(HardwareTest_t* test);

// Run all tests sequentially
void HardwareTest_RunAll(HardwareTest_t* test);

// Print summary
void HardwareTest_PrintSummary(HardwareTest_t* test);

// Continuous sensor monitoring mode
void HardwareTest_ContinuousMonitoring(HardwareTest_t* test);

#endif // HARDWARETEST_H