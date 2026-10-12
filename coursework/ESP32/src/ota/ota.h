#pragma once
#include <Arduino.h>

// Прапорець «йде оновлення».
// Читає main.cpp (не публікувати телеметрію) і майбутній код сну
// (не засинати посеред OTA).
// volatile: якщо колись винесете завантаження в окрему задачу FreeRTOS.
extern volatile bool otaInProgress;

// Обробити документ роботи, який віддав mqtt_take_job().
// Парсить, вирішує, качає, пише, звітує. Блокує на весь час завантаження.
// При успіху НЕ ПОВЕРТАЄТЬСЯ — усередині esp_deep_sleep_start().
void ota_handle_job(const char* jobDocument);
