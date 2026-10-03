#pragma once
#include <Arduino.h>

// Wi-Fi → час → сертифікати → сервер. Порядок критичний.
bool mqtt_begin();

// connect + subscribe. Підписка живе в сесії брокера:
// реконект = нова сесія = нуль підписок, тому вона всередині.
bool mqtt_connect();

bool mqtt_connected();

// Викликати кожну ітерацію loop(): читає вхідні байти й тримає Keep Alive.
void mqtt_poll();

// Реконект з паузою RECONNECT_INTERVAL. Сам підніме Wi-Fi, за потреби
// повторить синхронізацію часу і закриє стару TLS-сесію.
void mqtt_reconnect_tick();

void mqtt_publish_telemetry(const char* device_id, float temperature, float humidity, float lux);

// Генерична публікація для будь-якого модуля (наприклад, led — ack на
// виконання команди), щоб той не чіпав mqttClient напряму.
void mqtt_publish(const char* topic, const char* payload);

// NULL, якщо команди нема. Інакше — payload, дійсний до наступного mqtt_poll().
const char* mqtt_take_command();
