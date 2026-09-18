#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"

// ═══════════════════════════════════════════════════════════
// ПІНИ
// ═══════════════════════════════════════════════════════════
#define BUTTON_PIN 5
#define DHTT_PIN   4
#define DHTT_TYPE  DHT22

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ WI-FI
// ═══════════════════════════════════════════════════════════
#define WIFI_SSID     "Wokwi-GUEST"  // мережа Wokwi симулятора
#define WIFI_PASSWORD ""              // без пароля
#define WIFI_TIMEOUT  10000           // максимум 10 секунд на підключення

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ MQTT
// ═══════════════════════════════════════════════════════════
#define MQTT_BROKER    "broker.hivemq.com"          // публічний брокер HiveMQ
#define MQTT_PORT      1883                          // plain TCP, без TLS
#define MQTT_CLIENT_ID "esp32-ozoz03-a"              // унікальний — не як у ESP32-B!

#define TOPIC_BASE          "iot-course/ozoz03"
#define TOPIC_SENSOR_TEMP   TOPIC_BASE "/sensors/temperature"  // окремий підтопік — без розбору JSON
#define TOPIC_SENSOR_HUM    TOPIC_BASE "/sensors/humidity"     // окремий підтопік — без розбору JSON
#define TOPIC_COMMANDS      TOPIC_BASE "/commands"             // публікуємо "manual_read"
#define TOPIC_STATUS        TOPIC_BASE "/status/esp32-a"       // online/offline (retained, LWT)

// Таймер reconnect — чекаємо 5 секунд між спробами,
// максимум MQTT_MAX_RECONNECT_ATTEMPTS спроб поспіль. Якщо весь цикл спроб
// вичерпано (Wi-Fi/брокер недоступні довше) — пауза RECONNECT_COOLDOWN,
// потім лічильник скидається і цикл спроб починається знову. Так пристрій
// ніколи не "застрягає" офлайн назавжди без reboot.
unsigned long lastReconnectAttempt   = 0;
unsigned long reconnectCooldownStart = 0;
#define RECONNECT_INTERVAL       5000    // мс між спробами всередині циклу
#define MQTT_MAX_RECONNECT_ATTEMPTS 3    // спроб поспіль перед паузою
#define RECONNECT_COOLDOWN       60000   // мс паузи перед новим циклом спроб
int  mqttReconnectAttempts  = 0;
bool mqttReconnectExhausted = false;

// ═══════════════════════════════════════════════════════════
// ТАЙМЕР ПУБЛІКАЦІЇ
// ═══════════════════════════════════════════════════════════
#define PUBLISH_INTERVAL 10000  // публікуємо раз на 10 секунд

unsigned long lastPublish = 0;

// ── Debounce кнопки ──────────────────────────────────────
#define DEBOUNCE_DELAY 50  // мс
bool lastRawState = HIGH;
bool stableState  = HIGH;
unsigned long lastDebounceTime = 0;

// ═══════════════════════════════════════════════════════════
// MQTT / DHT КЛІЄНТИ
// ═══════════════════════════════════════════════════════════
// WiFiClient — TCP з'єднання
// PubSubClient — MQTT протокол поверх TCP
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);
DHT          dht(DHTT_PIN, DHTT_TYPE);

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

    // LWT: якщо з'єднання обірветься нештатно (не через штатний stop()),
    // брокер сам опублікує "offline" (retained) у TOPIC_STATUS — підписники
    // одразу побачать, що пристрій відпав, навіть без активного опитування
    if (mqttClient.connect(MQTT_CLIENT_ID, TOPIC_STATUS, 1, true, "offline")) {
        Serial.println(" OK");
        mqttClient.publish(TOPIC_STATUS, "online", true);  // retained
        return true;
    }

    // mqttClient.state() повертає код помилки:
    // -4 = таймаут, -2 = сервер не знайдено, 5 = відмовлено в доступі
    Serial.print(" помилка: ");
    Serial.println(mqttClient.state());
    return false;
}

// ═══════════════════════════════════════════════════════════
// ПУБЛІКАЦІЯ ДАНИХ СЕНСОРІВ
// ═══════════════════════════════════════════════════════════
void publishSensors() {
    float temperature = dht.readTemperature();
    float humidity    = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("[DHT] Помилка читання сенсора — пропускаємо публікацію");
        return;
    }

    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не підключено — пропускаємо");
        return;
    }

    // Кожен показник — окремим повідомленням у свій підтопік. Підписник
    // може слухати лише потрібне значення (наприклад, лише температуру)
    // без розбору JSON.
    char tempPayload[8];
    char humPayload[8];
    snprintf(tempPayload, sizeof(tempPayload), "%.1f", temperature);
    snprintf(humPayload,  sizeof(humPayload),  "%.1f", humidity);

    Serial.print("[MQTT] Публікуємо temperature=");
    Serial.print(tempPayload);
    Serial.print(" humidity=");
    Serial.println(humPayload);

    bool okTemp = mqttClient.publish(TOPIC_SENSOR_TEMP, tempPayload);
    bool okHum  = mqttClient.publish(TOPIC_SENSOR_HUM, humPayload);
    Serial.println((okTemp && okHum) ? "[MQTT] OK" : "[MQTT] Помилка публікації");
}

// ═══════════════════════════════════════════════════════════
// РУЧНИЙ ЗАПИТ — публікація "manual_read" при натисканні кнопки
// ═══════════════════════════════════════════════════════════
void publishManualRead() {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не підключено — команду manual_read не відправлено");
        return;
    }

    Serial.println("[MQTT] Публікуємо команду: manual_read");
    bool ok = mqttClient.publish(TOPIC_COMMANDS, "manual_read");
    Serial.println(ok ? "[MQTT] OK" : "[MQTT] Помилка публікації");
}

// ═══════════════════════════════════════════════════════════
// КНОПКА З DEBOUNCE
// ═══════════════════════════════════════════════════════════
void handleButton() {
    bool raw = digitalRead(BUTTON_PIN);

    if (raw != lastRawState) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        if (raw != stableState) {
            stableState = raw;

            if (stableState == LOW) {  // INPUT_PULLUP: LOW = кнопку натиснуто
                publishManualRead();
            }
        }
    }

    lastRawState = raw;
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32-A старт");

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    dht.begin();

    bool wifiOk = connectWifi();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setKeepAlive(60);        // PING кожні 60 секунд
    mqttClient.setSocketTimeout(30);    // таймаут TCP сокету 30 секунд
    if (wifiOk && connectMQTT()) {
        mqttReconnectAttempts = 0;
    }
    // Якщо Wi-Fi одразу не піднявся — connectMQTT() тут навіть не викликаємо
    // (все одно провалиться без мережі); maintainConnection() у loop()
    // сам піднімає Wi-Fi і потім MQTT.
}

// ═══════════════════════════════════════════════════════════
// ПІДТРИМКА З'ЄДНАННЯ — викликається з loop(), коли MQTT не підключено
// ═══════════════════════════════════════════════════════════
void maintainConnection() {
    unsigned long now = millis();

    if (mqttReconnectExhausted) {
        // Цикл із MQTT_MAX_RECONNECT_ATTEMPTS спроб вичерпано — чекаємо
        // паузу RECONNECT_COOLDOWN, потім стартуємо новий цикл спроб
        if (now - reconnectCooldownStart < RECONNECT_COOLDOWN) {
            return;
        }
        Serial.println("[MQTT] Пауза закінчилась — починаємо новий цикл спроб reconnect");
        mqttReconnectAttempts  = 0;
        mqttReconnectExhausted = false;
    }

    // Не штурмуємо мережу/брокер — чекаємо RECONNECT_INTERVAL між спробами
    if (now - lastReconnectAttempt <= RECONNECT_INTERVAL) return;
    lastReconnectAttempt = now;

    if (WiFi.status() != WL_CONNECTED) {
        // Без Wi-Fi спроба MQTT завідомо провалиться — не витрачаємо на це
        // лічильник спроб, а неблокуюче штовхаємо Wi-Fi реконект
        Serial.println("[Wi-Fi] З'єднання втрачено — перепідключаємось...");
        WiFi.reconnect();
        return;
    }

    mqttReconnectAttempts++;
    Serial.print("[MQTT] З'єднання втрачено — спроба ");
    Serial.print(mqttReconnectAttempts);
    Serial.print("/");
    Serial.println(MQTT_MAX_RECONNECT_ATTEMPTS);

    if (!connectMQTT() && mqttReconnectAttempts >= MQTT_MAX_RECONNECT_ATTEMPTS) {
        mqttReconnectExhausted = true;
        reconnectCooldownStart = now;
        Serial.print("[MQTT] Досягнуто максимум спроб reconnect — пауза ");
        Serial.print(RECONNECT_COOLDOWN / 1000);
        Serial.println("с перед новим циклом");
    }
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    handleButton();

    if (mqttClient.connected()) {
        // ОБОВ'ЯЗКОВО — підтримує Keep Alive з'єднання з брокером
        mqttClient.loop();

        mqttReconnectAttempts  = 0;
        mqttReconnectExhausted = false;

        // Публікуємо дані кожні PUBLISH_INTERVAL мс
        unsigned long now = millis();
        if ((now - lastPublish) > PUBLISH_INTERVAL) {
            lastPublish = now;
            publishSensors();
        }
    } else {
        maintainConnection();
    }
}
