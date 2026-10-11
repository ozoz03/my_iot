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

// Реконект з експоненційним backoff (1с → 2с → 4с → ... → стеля 30с,
// скидається до 1с після вдалого конекту). Сам підніме Wi-Fi, за потреби
// повторить синхронізацію часу і закриє стару TLS-сесію.
void mqtt_reconnect_tick();

void mqtt_publish_telemetry(const char* device_id, float temperature, float humidity, float lux);

// Генерична публікація для будь-якого модуля (наприклад, led — ack на
// виконання команди), щоб той не чіпав mqttClient напряму.
void mqtt_publish(const char* topic, const char* payload);

// NULL, якщо команди нема. Інакше — payload, дійсний до наступного mqtt_poll().
const char* mqtt_take_command();

// ── Роботи / OTA ────────────────────────────────────────────
// Віддаємо СИРИЙ документ роботи. Парсить його ota.cpp.
const char* mqtt_take_job();       // NULL, якщо роботи немає

// Публікація в $next/get — «дай мою наступну роботу».
// Викликається при кожному підключенні (у mqtt_connect).
void mqtt_request_next_job();

// Звіт про статус конкретної роботи.
// status: "IN_PROGRESS" | "SUCCEEDED" | "FAILED"
void mqtt_report_status(const char* jobId, const char* status);

// Розірвати MQTT-з'єднання перед завантаженням прошивки.
// Звільняє TLS-контекст (десятки КБ купи) під другий, HTTPS до S3.
// ota.cpp не має лізти в приватний net клієнта — робимо це тут.
void mqtt_disconnect_for_ota();
