#pragma once
#include <Arduino.h>

// Фоторезистор (ldr1 у diagram.json), живлення 3V3
#define LDR_AO_PIN  34   // аналоговий: ADC1, тільки input

void ldr_begin();

// ADC (0..4095) -> люкси
float ldr_read_lux();
