#include <Arduino.h>
#include "FreqCountESP.h"

int inputPin = 14;
int timerMs = 1000;
int const duration = 30;
int freqArr[duration];
int freqArrIndex = 0;

float frequencyAVG()
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

    Serial.print("30 sec Freq avg: ");
    Serial.print(frequencyAVG());
    Serial.println(" Hz");
    Serial.print("30 sec Knots avg: ");
    Serial.print(frequencyAVG() / 10);
    Serial.println(" kts");
    Serial.print("30 sec m/s avg: ");
    Serial.print(frequencyAVG() / 10 * 0.514444444);
    Serial.println(" m/s");
}

void measureFreq()
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

void setup()
{
    Serial.begin(115200);
    FreqCountESP.begin(inputPin, timerMs);
}

void loop()
{
    measureFreq();
}