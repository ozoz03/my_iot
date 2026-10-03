#pragma once
#include <Arduino.h>

#define LED_PIN 2

void led_begin();

// Очікуваний payload: {"action":"set","value":"on"|"off"}
void led_handle_command(const char* cmd);
