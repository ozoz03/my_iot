#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "secrets.h"

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ WI-FI
// ═══════════════════════════════════════════════════════════
#define WIFI_TIMEOUT  10000           // максимум 10 секунд на підключення

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ MQTT (AWS IoT Core)
// ═══════════════════════════════════════════════════════════
#define MQTT_PORT      8883                            // MQTT over TLS, НЕ 1883!
#define TOPIC_TELEMETRY "iot-course/ozasymenko/sensors/data"     // топік для публікації

// Таймер reconnect — чекаємо 5 секунд між спробами
unsigned long lastReconnectAttempt = 0;
#define RECONNECT_INTERVAL 5000  // мс

// ═══════════════════════════════════════════════════════════
// ТАЙМЕР ПУБЛІКАЦІЇ
// ═══════════════════════════════════════════════════════════
#define PUBLISH_INTERVAL 30000  // публікуємо раз на 30 секунд

unsigned long lastPublish = 0;

// ═══════════════════════════════════════════════════════════
// MQTT + TLS КЛІЄНТ
// ═══════════════════════════════════════════════════════════
// WiFiClientSecure замість WiFiClient — ЄДИНА зміна на цьому рівні (слайд 17)
// PubSubClient — той самий, що й у Занятті 8, без жодних змін
WiFiClientSecure net;
PubSubClient      mqttClient(net);

// ═══════════════════════════════════════════════════════════
// WI-FI — без змін із Заняття 8
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
// NTP — синхронізація часу (слайд 18)
// БЕЗ ЦЬОГО TLS ВПАДЕ, навіть з правильними сертифікатами:
// ESP32 стартує з 1970 року, і handshake вважає сертифікат AWS
// "ще не дійсним", бо 1970 < дата видачі сертифіката
// ═══════════════════════════════════════════════════════════
#define NTP_TIMEOUT 15000  // мс

bool syncTime() {
    Serial.print("[NTP] Синхронізація часу");
    configTime(0, 0, "pool.ntp.org");  // зсув 0, DST 0 — для TLS достатньо

    struct tm timeinfo;
    unsigned long start = millis();
    while (!getLocalTime(&timeinfo)) {
        if (millis() - start > NTP_TIMEOUT) {
            Serial.println(" таймаут!");
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println(" OK");
    return true;
}

// ═══════════════════════════════════════════════════════════
// ПІДКЛЮЧЕННЯ ДО AWS IOT CORE
// Порядок кроків критичний: Wi-Fi → час → сертифікати → сервер
// ═══════════════════════════════════════════════════════════
void connectAWS() {
    // 1. Wi-Fi
    connectWifi();

    // 2. НОВЕ: синхронізуємо час — до сертифікатів, до connect()
    syncTime();

    // 3. НОВЕ: заряджаємо три файли зі слайда 10 в TLS-клієнт
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);

    // 4. Вказуємо брокер: наш AWS endpoint, порт 8883
    mqttClient.setServer(AWS_IOT_ENDPOINT, MQTT_PORT);

    // Буфер PubSubClient за замовчуванням 256 байт — замалий,
    // мовчки обрізає JSON. Збільшуємо до підключення.
    mqttClient.setBufferSize(512);
}

// ═══════════════════════════════════════════════════════════
// MQTT CONNECT
// Client ID = THINGNAME — Policy обмежує Connect саме по ньому (слайд 16)
// ═══════════════════════════════════════════════════════════
bool connectMQTT() {
    Serial.print("[MQTT] Підключаємось до AWS IoT Core...");

    if (mqttClient.connect(THINGNAME)) {
        Serial.println(" OK");
        return true;
    }

    // mqttClient.state() коди: -2 = TLS/handshake помилка
    // (перевір час і endpoint), 5 = відмовлено (перевір Policy)
    Serial.print(" помилка: ");
    Serial.println(mqttClient.state());
    return false;
}

// ═══════════════════════════════════════════════════════════
// ПУБЛІКАЦІЯ ДАНИХ — без змін із Заняття 8
// ═══════════════════════════════════════════════════════════
void publishData(float temperature, float humidity) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не підключено — пропускаємо");
        return;
    }

    // snprintf замість String — безпечно для heap (Заняття 4)
    char payload[80];
    snprintf(payload, sizeof(payload),
        "{\"temperature\":%.1f,\"humidity\":%.1f}",
        temperature, humidity);

    Serial.print("[MQTT] Публікуємо: ");
    Serial.println(payload);

    bool ok = mqttClient.publish(TOPIC_TELEMETRY, payload);
    Serial.println(ok ? "[MQTT] OK" : "[MQTT] Помилка публікації");
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32-A (AWS IoT Core edition) старт");

    connectAWS();

    mqttClient.setKeepAlive(60);        // PING кожні 60 секунд
    mqttClient.setSocketTimeout(30);    // таймаут TCP сокету 30 секунд

    connectMQTT();
}

// ═══════════════════════════════════════════════════════════
// LOOP — логіка ІДЕНТИЧНА Заняттю 8
// ═══════════════════════════════════════════════════════════
void loop() {
    float temperature = dht.readTemperature();
    float humidity    = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("[DHT] Помилка читання сенсора — пропускаємо публікацію");
        return;
    }

    if (mqttClient.connected()) {
        mqttClient.loop();  // ОБОВ'ЯЗКОВО — підтримує Keep Alive

        unsigned long now = millis();
        if ((now - lastPublish) > PUBLISH_INTERVAL) {
            lastPublish = now;
            publishData(temperature, humidity);
        }
    } else {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            Serial.println("[MQTT] З'єднання втрачено — перепідключаємось...");

            // Явно закриваємо стару TLS-сесію перед новою спробою —
            // інакше mbedTLS-контекст може лишитись "напівживим"
            // і з часом призвести до витоку пам'яті (Заняття 4)
            net.stop();

            connectMQTT();
        }
    }
}