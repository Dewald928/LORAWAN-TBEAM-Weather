#include "sensors.h"

DynamicJsonDocument jsonBuffer(1024); // serial output of readings

// Local logging tag
static const char TAG[] = __FILE__;

uint8_t batt_level = 0; // display value
uint16_t voltage = 0;
int freq_time_ms = 1000;
int const duration = 30;
int freqArr[duration];
int freqArrIndex = 0;

#ifdef BAT_MEASURE_ADC
esp_adc_cal_characteristics_t *adc_characs =
    (esp_adc_cal_characteristics_t *)calloc(
        1, sizeof(esp_adc_cal_characteristics_t));

#ifndef BAT_MEASURE_ADC_UNIT // ADC1
static const adc1_channel_t adc_channel = BAT_MEASURE_ADC;
#else // ADC2
static const adc2_channel_t adc_channel = BAT_MEASURE_ADC;
RTC_NOINIT_ATTR uint64_t RTC_reg_b;
#endif
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;
#endif // BAT_MEASURE_ADC

void init_sensors()
{
    FreqCountESP.begin(PIN_WINDSPEED, freq_time_ms);
}

void calibrate_voltage(void)
{
#ifdef BAT_MEASURE_ADC
// configure ADC
#ifndef BAT_MEASURE_ADC_UNIT // ADC1
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(adc_channel, atten);
#else // ADC2
    adc2_config_channel_atten(adc_channel, atten);
    // ADC2 wifi bug workaround, see
    // https://github.com/espressif/arduino-esp32/issues/102
    RTC_reg_b = READ_PERI_REG(SENS_SAR_READ_CTRL2_REG);
#endif
    // calibrate ADC
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
        unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_characs);
    // show ADC characterization base
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP)
    {
        ESP_LOGI(TAG,
                 "ADC characterization based on Two Point values stored in eFuse");
    }
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF)
    {
        ESP_LOGI(TAG,
                 "ADC characterization based on reference voltage stored in eFuse");
    }
    else
    {
        ESP_LOGI(TAG, "ADC characterization based on default reference voltage");
    }
#endif
}

uint16_t read_voltage(void)
{
    uint16_t voltage = 0;

#ifdef BAT_MEASURE_ADC
    // multisample ADC
    uint32_t adc_reading = 0;
#ifndef BAT_MEASURE_ADC_UNIT // ADC1
    for (int i = 0; i < NO_OF_SAMPLES; i++)
    {
        adc_reading += adc1_get_raw(adc_channel);
    }
#else  // ADC2
    int adc_buf = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++)
    {
        // ADC2 wifi bug workaround, see
        // https://github.com/espressif/arduino-esp32/issues/102
        WRITE_PERI_REG(SENS_SAR_READ_CTRL2_REG, RTC_reg_b);
        SET_PERI_REG_MASK(SENS_SAR_READ_CTRL2_REG, SENS_SAR2_DATA_INV);
        adc2_get_raw(adc_channel, ADC_WIDTH_BIT_12, &adc_buf);
        adc_reading += adc_buf;
    }
#endif // BAT_MEASURE_ADC_UNIT
    adc_reading /= NO_OF_SAMPLES;
    // Convert ADC reading to voltage in mV
    voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_characs);
    voltage -= 73; // add offset
#endif             // BAT_MEASURE_ADC

#ifdef BAT_VOLTAGE_DIVIDER
    voltage *= BAT_VOLTAGE_DIVIDER;
#endif // BAT_VOLTAGE_DIVIDER

    return voltage;
}

uint32_t frequencyAVG()
{
    int sum = 0;
    for (int i = 0; i < duration; i++)
    {
        sum += freqArr[i];
    }
    return (float)sum / duration;
}

void serialLogFrequency(uint32_t frequency)
{
    Serial.print("Frequency: ");
    Serial.print(frequency);
    Serial.println(" Hz");
    Serial.print("Knots: ");
    Serial.print(frequency / 10);
    Serial.println(" kts");
    Serial.print("Meters/second: ");
    Serial.print(frequency / 10 * 0.514444444);
    Serial.println(" m/s");
}

void read_frequency()
{
    if (FreqCountESP.available())
    {
        uint32_t frequency = FreqCountESP.read();
        freqArr[freqArrIndex] = frequency;
        freqArrIndex++;
        if (freqArrIndex >= duration)
        {
            freqArrIndex = 0;
        }
        serialLogFrequency(frequency);
    }
}

void take_readings()
{
    // Switch off wifi and bluetooth before taking readings
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);

    // Read voltage
    // Serial.print("Voltage: ");
    // calibrate_voltage();
    // voltage = read_voltage();
    // Serial.print(voltage);
    // Serial.println(" mV");

    // Read frequency
    for (int i = 0; i < duration; i++)
    {
        read_frequency();
    }

    Serial.print("30 sec Freq avg: ");
    Serial.print(frequencyAVG());
    Serial.println(" Hz");
    Serial.print("30 sec Knots avg: ");
    Serial.print(frequencyAVG() / 10);
    Serial.println(" kts");
    Serial.print("30 sec m/s avg: ");
    Serial.print(frequencyAVG() / 10 * 0.514444444);
    Serial.println(" m/s");

    // Read battery voltage
    uint16_t battery_voltage = getBatteryVoltage();
    Serial.print("Battery voltage: ");
    Serial.print(battery_voltage);
    Serial.println(" mV");

    // check_WiFi(); // Reconnect to WiFi, if needed

    // Encode the data to send to Cayenne
    JsonObject root = jsonBuffer.to<JsonObject>();
    Serial.println();
    lpp.reset();
    lpp.addLuminosity(1, battery_voltage);
    lpp.addFrequency(2, frequencyAVG());
    lpp.addAnalogOutput(3, frequencyAVG() / 10 * 0.514444444);

    lpp.decodeTTN(lpp.getBuffer(), lpp.getSize(), root);

    // serializeJsonPretty(root, Serial);
    Serial.println();
}
