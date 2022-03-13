// BLOG Eletrogate
// ESP32 Frequency Meter
// ESP32 DevKit 38 pins + LCD
// https://blog.eletrogate.com/esp32-frequencimetro-de-precisao
// Rui Viana and Gustavo Murta august/2020
#include <Arduino.h>
#include "stdio.h"       // Library STDIO
#include "driver/ledc.h" // Library ESP32 LEDC
#include "driver/pcnt.h" // Library ESP32 PCNT

#define LCD_OFF     // LCD_ON, if want use LCD 4 bits interface
#define LCD_I2C_OFF // LCD_I2C_ON, if want use I2C LCD (PCF8574)

#ifdef LCD_I2C_ON                  // If I2C LCD enabled
#define I2C_SDA 21                 // LCD I2C SDA - GPIO_21
#define I2C_SCL 22                 // LCD I2C SCL - GPIO_22
#include <Wire.h>                  // I2C Libray
#include <LiquidCrystal_PCF8574.h> // Library LCD with PCF8574
LiquidCrystal_PCF8574 lcd(0x3F);   // Instance LCD I2C - adress x3F
#endif                             // LCD I2C

#ifdef LCD_ON                            // LCD with 4 bits interface enabled
#include <LiquidCrystal.h>               // Library LCD
LiquidCrystal lcd(4, 16, 17, 5, 18, 19); // Instance - ports RS,ENA,D4,D5,D6,D7
#endif                                   // LCD

#define PCNT_COUNT_UNIT PCNT_UNIT_0       // Set Pulse Counter Unit - 0
#define PCNT_COUNT_CHANNEL PCNT_CHANNEL_0 // Set Pulse Counter channel - 0

#define PCNT_INPUT_SIG_IO GPIO_NUM_34   // Set Pulse Counter input - Freq Meter Input GPIO 34
#define LEDC_HS_CH0_GPIO GPIO_NUM_33    // Saida do LEDC - gerador de pulsos - GPIO_33
#define PCNT_INPUT_CTRL_IO GPIO_NUM_35  // Set Pulse Counter Control GPIO pin - HIGH = count up, LOW = count down
#define OUTPUT_CONTROL_GPIO GPIO_NUM_32 // Timer output control port - GPIO_32
#define PCNT_H_LIM_VAL overflow         // Overflow of Pulse Counter

#define IN_BOARD_LED GPIO_NUM_2 // ESP32 native LED - GPIO 2

bool flag = true;               // Flag to enable print frequency reading
uint32_t overflow = 20000;      // Max Pulse Counter value
int16_t pulses = 0;             // Pulse Counter value
uint32_t multPulses = 0;        // Quantidade de overflows do contador PCNT
uint32_t sample_time = 1000000; // sample time of 1 second to count pulses
uint32_t osc_freq = 12543;      // Oscillator frequency - initial 12543 Hz (may be 1 Hz to 40 MHz)
uint32_t mDuty = 0;             // Duty value
uint32_t resolution = 0;        // Resolution value
float frequency = 0;            // frequency value
char buf[32];                   // Buffer

esp_timer_create_args_t create_args; // Create an esp_timer instance
esp_timer_handle_t timer_handle;     // Create an single timer

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED; // portMUX_TYPE to do synchronism

//----------------------------------------------------------------------------------
void read_PCNT(void *p) // Read Pulse Counter
{
    gpio_set_level(OUTPUT_CONTROL_GPIO, 0);           // Stop counter - output control LOW
    pcnt_get_counter_value(PCNT_COUNT_UNIT, &pulses); // Read Pulse Counter value
    flag = true;                                      // Change flag to enable print
}

//----------------------------------------------------------------------------------------
char *ultos_recursive(unsigned long val, char *s, unsigned radix, int pos) // Format an unsigned long (32 bits) into a string
{
    int c;
    if (val >= radix)
        s = ultos_recursive(val / radix, s, radix, pos + 1);
    c = val % radix;
    c += (c < 10 ? '0' : 'a' - 10);
    *s++ = c;
    if (pos % 3 == 0)
        *s++ = ',';
    return s;
}
//----------------------------------------------------------------------------------------
char *ltos(long val, char *s, int radix) // Format an long (32 bits) into a string
{
    if (radix < 2 || radix > 36)
    {
        s[0] = 0;
    }
    else
    {
        char *p = s;
        if (radix == 10 && val < 0)
        {
            val = -val;
            *p++ = '-';
        }
        p = ultos_recursive(val, p, radix, 0) - 1;
        *p = 0;
    }
    return s;
}

//----------------------------------------------------------------------------
void init_osc_freq() // Initialize Oscillator to test Freq Meter
{
    resolution = (log(80000000 / osc_freq) / log(2)) / 2; // Calc of resolution of Oscillator
    if (resolution < 1)
        resolution = 1; // set min resolution
    // Serial.println(resolution);                                          // Print
    mDuty = (pow(2, resolution)) / 2; // Calc of Duty Cycle 50% of the pulse
    // Serial.println(mDuty);                                               // Print

    ledc_timer_config_t ledc_timer = {}; // LEDC timer config instance

    ledc_timer.duty_resolution = ledc_timer_bit_t(resolution); // Set resolution
    ledc_timer.freq_hz = osc_freq;                             // Set Oscillator frequency
    ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;              // Set high speed mode
    ledc_timer.timer_num = LEDC_TIMER_0;                       // Set LEDC timer index - 0
    ledc_timer_config(&ledc_timer);                            // Set LEDC Timer config

    ledc_channel_config_t ledc_channel = {}; // LEDC Channel config instance

    ledc_channel.channel = LEDC_CHANNEL_0;          // Set HS Channel - 0
    ledc_channel.duty = mDuty;                      // Set Duty Cycle 50%
    ledc_channel.gpio_num = LEDC_HS_CH0_GPIO;       // LEDC Oscillator output GPIO 33
    ledc_channel.intr_type = LEDC_INTR_DISABLE;     // LEDC Fade interrupt disable
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE; // Set LEDC high speed mode
    ledc_channel.timer_sel = LEDC_TIMER_0;          // Set timer source of channel - 0
    ledc_channel_config(&ledc_channel);             // Config LEDC channel
}

//----------------------------------------------------------------------------------
static void IRAM_ATTR pcnt_intr_handler(void *arg) // Counting overflow pulses
{
    portENTER_CRITICAL_ISR(&timerMux);       // disabling the interrupts
    multPulses++;                            // increment Overflow counter
    PCNT.int_clr.val = BIT(PCNT_COUNT_UNIT); // Clear Pulse Counter interrupt bit
    portEXIT_CRITICAL_ISR(&timerMux);        // enabling the interrupts
}

//----------------------------------------------------------------------------------
void init_PCNT(void) // Initialize and run PCNT unit
{
    pcnt_config_t pcnt_config = {}; // PCNT unit instance

    pcnt_config.pulse_gpio_num = PCNT_INPUT_SIG_IO; // Pulse input GPIO 34 - Freq Meter Input
    pcnt_config.ctrl_gpio_num = PCNT_INPUT_CTRL_IO; // Control signal input GPIO 35
    pcnt_config.unit = PCNT_COUNT_UNIT;             // Unidade de contagem PCNT - 0
    pcnt_config.channel = PCNT_COUNT_CHANNEL;       // PCNT unit number - 0
    pcnt_config.counter_h_lim = PCNT_H_LIM_VAL;     // Maximum counter value - 20000
    pcnt_config.pos_mode = PCNT_COUNT_INC;          // PCNT positive edge count mode - inc
    pcnt_config.neg_mode = PCNT_COUNT_INC;          // PCNT negative edge count mode - inc
    pcnt_config.lctrl_mode = PCNT_MODE_DISABLE;     // PCNT low control mode - disable
    pcnt_config.hctrl_mode = PCNT_MODE_KEEP;        // PCNT high control mode - won't change counter mode
    pcnt_unit_config(&pcnt_config);                 // Initialize PCNT unit

    pcnt_counter_pause(PCNT_COUNT_UNIT); // Pause PCNT unit
    pcnt_counter_clear(PCNT_COUNT_UNIT); // Clear PCNT unit

    pcnt_event_enable(PCNT_COUNT_UNIT, PCNT_EVT_H_LIM);  // Enable event to watch - max count
    pcnt_isr_register(pcnt_intr_handler, NULL, 0, NULL); // Setup Register ISR handler
    pcnt_intr_enable(PCNT_COUNT_UNIT);                   // Enable interrupts for PCNT unit

    pcnt_counter_resume(PCNT_COUNT_UNIT); // Resume PCNT unit - starts count
}

//---------------------------------------------------------------------------------
void init_frequencyMeter()
{
    init_osc_freq(); // Initialize Oscillator
    init_PCNT();     // Initialize and run PCNT unit

    gpio_pad_select_gpio(OUTPUT_CONTROL_GPIO);                 // Set GPIO pad
    gpio_set_direction(OUTPUT_CONTROL_GPIO, GPIO_MODE_OUTPUT); // Set GPIO 32 as output

    create_args.callback = read_PCNT;              // Set esp-timer argument
    esp_timer_create(&create_args, &timer_handle); // Create esp-timer instance

    gpio_set_direction(IN_BOARD_LED, GPIO_MODE_OUTPUT); // Set LED inboard as output

    gpio_matrix_in(PCNT_INPUT_SIG_IO, SIG_IN_FUNC226_IDX, false);    // Set GPIO matrin IN - Freq Meter input
    gpio_matrix_out(IN_BOARD_LED, SIG_IN_FUNC226_IDX, false, false); // Set GPIO matrix OUT - to inboard LED
}

//----------------------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);                                 // Serial Console Arduino 115200 Bps
    Serial.println(" Input the Frequency - 1 to 40 MHz"); // Console print

#ifdef LCD_I2C_ON                 // If usinf I2C LCD
    Wire.begin(I2C_SDA, I2C_SCL); // Begin I2C Interface
    lcd.setBacklight(255);        // Set I2C LCD Backlight ON
#endif

#if defined LCD_ON || defined LCD_I2C_ON // If using LCD or I2C LCD
    lcd.begin(16, 2);                    // LCD init 16 x2
    lcd.print("  Frequency:");           // LCD print
#endif

    init_frequencyMeter(); // Initialize Frequency Meter
}

//---------------------------------------------------------------------------------
void loop()
{
    if (flag == true) // If count has ended
    {
        flag = false;                                         // change flag to disable print
        frequency = (pulses + (multPulses * overflow)) / 2;   // Calculation of frequency
        printf("Frequency : %s", (ltos(frequency, buf, 10))); // Print frequency with commas
        printf(" Hz \n");                                     // Print unity Hz

#if defined LCD_ON || defined LCD_I2C_ON       // If using LCD or I2C LCD
        lcd.setCursor(2, 1);                   // Set cursor position - column and row
        lcd.print((ltos(frequency, buf, 10))); // LCD print frequency
        lcd.print(" Hz              ");        // LCD print unity Hz
#endif

        multPulses = 0; // Clear overflow counter
        // Put your function here, if you want
        delay(100); // Delay 100 ms
        // Put your function here, if you want

        pcnt_counter_clear(PCNT_COUNT_UNIT);             // Clear Pulse Counter
        esp_timer_start_once(timer_handle, sample_time); // Initialize High resolution timer (1 sec)
        gpio_set_level(OUTPUT_CONTROL_GPIO, 1);          // Set enable PCNT count
    }

    String inputString = ""; // clear temporary string
    osc_freq = 0;            // Clear oscillator frequency
    while (Serial.available())
    {
        char inChar = (char)Serial.read(); // Reads a byte on the console
        inputString += inChar;             // Add char to string
        if (inChar == '\n')                // If new line (enter)
        {
            osc_freq = inputString.toInt(); // Converts String into integer value
            inputString = "";               // Clear string
        }
    }
    if (osc_freq != 0) // If some value inputted to oscillator frequency
    {
        init_osc_freq(); // reconfigure ledc function - oscillator
    }
}