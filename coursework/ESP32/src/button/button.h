#pragma once
#include <Arduino.h>

#define BUTTON_PIN     5   // другий контакт -> GND, тому INPUT_PULLUP
#define DEBOUNCE_DELAY 50  // мс

void button_begin();

// true рівно один раз на кожне натискання (антидребезг по фронту,
// повторний виклик до відпускання й нового натискання поверне false)
bool button_pressed();
