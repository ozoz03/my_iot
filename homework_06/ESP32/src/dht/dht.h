#pragma once
#include <Arduino.h>

#define DHT_PIN  4

void dht_begin();

// Повертає true, якщо обидва значення валідні (не NaN).
// temperature/humidity лишаються попередніми при невдалому читанні —
// рішення, публікувати їх чи ні, приймає викликач.
bool dht_read(float &temperature, float &humidity);
