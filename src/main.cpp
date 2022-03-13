// Copyright (c) 2021 Dewald Krynauw. All rights reserved.

#include <Arduino.h>
#include <CayenneLPP.h> // Cayenne-LPP library
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "sensors.h"
#include "config.h"
#include "lorawan.h"
#include "boards.h"

CayenneLPP lpp(51);

void setup()
{
    initBoard();

    // Init sensors
    init_sensors();

    // Init lorawan
    setupLMIC();
}

void loop()
{
    // Send and Receive data from the LoRaWAN network
    loopLMIC();
}
