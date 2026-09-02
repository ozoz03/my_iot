#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ WI-FI
// ═══════════════════════════════════════════════════════════
#define WIFI_SSID     "Wokwi-GUEST"  // мережа Wokwi симулятора
#define WIFI_PASSWORD ""              // без пароля
#define WIFI_TIMEOUT  10000           // максимум 10 секунд на підключення

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ MQTT
// ═══════════════════════════════════════════════════════════
#define MQTT_BROKER    "broker.hivemq.com"       // публічний брокер HiveMQ
#define MQTT_PORT      1883                       // plain TCP, без TLS
#define MQTT_CLIENT_ID "esp32-demo-a"             // унікальний — не як у ESP32-B!
#define TOPIC_SENSORS  "iot-course/demo/sensors"  // топік для публікації

// Таймер reconnect — чекаємо 5 секунд між спробами
// щоб не штурмувати брокер при нестабільному з'єднанні
unsigned long lastReconnectAttempt = 0;
#define RECONNECT_INTERVAL 5000  // мс

// ═══════════════════════════════════════════════════════════
// ТАЙМЕР ПУБЛІКАЦІЇ
// ═══════════════════════════════════════════════════════════
#define PUBLISH_INTERVAL 10000  // публікуємо раз на 10 секунд

unsigned long lastPublish = 0;

// ═══════════════════════════════════════════════════════════
// MQTT КЛІЄНТ
// ═══════════════════════════════════════════════════════════
// WiFiClient — TCP з'єднання
// PubSubClient — MQTT протокол поверх TCP
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// ═══════════════════════════════════════════════════════════
// WI-FI
// ═══════════════════════════════════════════════════════════
bool connectWifi() {
    Serial.print("[Wi-Fi] Підключаємось");
    // Канал 6 — пропускає сканування, економить ~4 секунди в Wokwi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_TIMEOUT) {
            Serial.println(" таймаут!");
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println(" OK");
    Serial.print("[Wi-Fi] IP: ");
    Serial.println(WiFi.localIP());
    return true;
}

// ═══════════════════════════════════════════════════════════
// MQTT
// ═══════════════════════════════════════════════════════════
bool connectMQTT() {
    Serial.print("[MQTT] Підключаємось до ");
    Serial.print(MQTT_BROKER);
    Serial.print("...");

    if (mqttClient.connect(MQTT_CLIENT_ID)) {
        Serial.println(" OK");
        return true;
    }

    // mqttClient.state() повертає код помилки:
    // -4 = таймаут, -2 = сервер не знайдено, 5 = відмовлено в доступі
    Serial.print(" помилка: ");
    Serial.println(mqttClient.state());
    return false;
}

// ═══════════════════════════════════════════════════════════
// ПУБЛІКАЦІЯ ДАНИХ
// ═══════════════════════════════════════════════════════════
void publishData(float temperature, float humidity) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не підключено — пропускаємо");
        return;
    }

    // snprintf замість String — безпечно для heap (Заняття 4)
    // char буфер фіксованого розміру, ніякої фрагментації
    char payload[80];
    snprintf(payload, sizeof(payload),
        "{\"temperature\":%.1f,\"humidity\":%.1f}",
        temperature, humidity);

    Serial.print("[MQTT] Публікуємо: ");
    Serial.println(payload);

    // publish() повертає true якщо повідомлення прийнято брокером
    bool ok = mqttClient.publish(TOPIC_SENSORS, payload);
    Serial.println(ok ? "[MQTT] OK" : "[MQTT] Помилка публікації");
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32-A старт");

    connectWifi();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setKeepAlive(60);        // PING кожні 60 секунд
    mqttClient.setSocketTimeout(30);    // таймаут TCP сокету 30 секунд
    connectMQTT();
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    if (mqttClient.connected()) {
        // ОБОВ'ЯЗКОВО — підтримує Keep Alive з'єднання з брокером
        mqttClient.loop();

        // Публікуємо дані кожні PUBLISH_INTERVAL мс
        // random(15, 40) — симулюємо зміну температури для демонстрації
        // Замінити на реальні дані з DHT22
        unsigned long now = millis();
        if ((now - lastPublish) > PUBLISH_INTERVAL) {
            lastPublish = now;
            publishData(random(15, 40), 55.0);
        }
    } else {
        // millis() таймер між спробами reconnect
        // Не штурмуємо брокер — чекаємо RECONNECT_INTERVAL мс
        unsigned long now = millis();
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            Serial.println("[MQTT] З'єднання втрачено — перепідключаємось...");
            connectMQTT();
        }
    }
}
