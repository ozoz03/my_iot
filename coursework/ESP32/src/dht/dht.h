#pragma once
#include <Arduino.h>

#define DHT_PIN  4

// DHT22: мінімальний інтервал між читаннями за датащитом — ~2с.
// Опитувати частіше безглузде: датчик або поверне той самий кеш, або
// помилку. Межа для sleepSeconds, що приходить із Shadow desired.interval.
#define DHT_MIN_INTERVAL_S 2

void dht_begin();

// Повертає true, якщо обидва значення валідні (не NaN).
// temperature/humidity лишаються попередніми при невдалому читанні —
// рішення, публікувати їх чи ні, приймає викликач.
bool dht_read(float &temperature, float &humidity);
