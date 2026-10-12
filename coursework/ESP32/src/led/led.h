#pragma once
#include <Arduino.h>

#define LED_PIN 2

void led_begin();

// Очікуваний payload: {"action":"set","value":"on"|"off"}
void led_handle_command(const char* cmd);

// Прямий сеттер без JSON — "on" / "off". Використовує Device Shadow:
// там значення вже витягнуте парсером, і ганяти його назад у JSON,
// щоб знову розібрати рядком, було б безглуздо.
void led_set(const char* state);
