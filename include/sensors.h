#pragma once

#include <Arduino.h>
#include <esp_adc_cal.h>
#include <WiFi.h>
#include "FreqCountESP.h"

#include "config.h"
#include "lorawan.h"
#include "wifi_helper.h"

#define BAT_MEASURE_ADC_UNIT 1;
#define BAT_MEASURE_ADC ADC2_GPIO25_CHANNEL // battery probe GPIO pin -> PIN_PRESSURE
// #define BAT_VOLTAGE_DIVIDER 2 // voltage divider 100k/100k on board

#define DEFAULT_VREF 1100 // tbd: use adc2_vref_to_gpio() for better estimate
#define NO_OF_SAMPLES 64  // we do some multisampling to get better values

#ifdef BAT_MEASURE_ADC_UNIT // ADC2 wifi bug workaround
extern RTC_NOINIT_ATTR uint64_t RTC_reg_b;
#include "soc/sens_reg.h" // needed for adc pin reset
#endif

void calibrate_voltage(void);
void init_sensors();
uint16_t read_voltage(void);
void take_readings();

extern uint16_t voltage; // voltage in mV of pressure sensor

typedef uint8_t (*mapFn_t)(uint16_t, uint16_t, uint16_t);

// The following map functions were taken from
//
// Battery.h - Battery library
// Copyright (c) 2014 Roberto Lo Giacco
// https://github.com/rlogiacco/BatterySense

/**
 * Symmetric sigmoidal approximation
 * https://www.desmos.com/calculator/7m9lu26vpy
 *
 * c - c / (1 + k*x/v)^3
 */
static inline uint8_t sigmoidal(uint16_t voltage, uint16_t minVoltage,
                                uint16_t maxVoltage)
{
    // slow
    // uint8_t result = 110 - (110 / (1 + pow(1.468 * (voltage -
    // minVoltage)/(maxVoltage - minVoltage), 6)));

    // steep
    // uint8_t result = 102 - (102 / (1 + pow(1.621 * (voltage -
    // minVoltage)/(maxVoltage - minVoltage), 8.1)));

    // normal
    uint8_t result = 105 - (105 / (1 + pow(1.724 * (voltage - minVoltage) /
                                               (maxVoltage - minVoltage),
                                           5.5)));
    return result >= 100 ? 100 : result;
}

/**
 * Asymmetric sigmoidal approximation
 * https://www.desmos.com/calculator/oyhpsu8jnw
 *
 * c - c / [1 + (k*x/v)^4.5]^3
 */
static inline uint8_t asigmoidal(uint16_t voltage, uint16_t minVoltage,
                                 uint16_t maxVoltage)
{
    uint8_t result = 101 - (101 / pow(1 + pow(1.33 * (voltage - minVoltage) /
                                                  (maxVoltage - minVoltage),
                                              4.5),
                                      3));
    return result >= 100 ? 100 : result;
}

/**
 * Linear mapping
 * https://www.desmos.com/calculator/sowyhttjta
 *
 * x * 100 / v
 */
static inline uint8_t linear(uint16_t voltage, uint16_t minVoltage,
                             uint16_t maxVoltage)
{
    return (unsigned long)(voltage - minVoltage) * 100 /
           (maxVoltage - minVoltage);
}

static inline float mapf(float val, float in_min, float in_max, float out_min, float out_max)
{
    return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
