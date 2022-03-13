#pragma once

#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <CayenneLPP.h> // Cayenne-LPP library
#include "boards.h"
#include "sensors.h"

extern CayenneLPP lpp;
extern unsigned TX_INTERVAL;

void setupLMIC(void);
void loopLMIC(void);