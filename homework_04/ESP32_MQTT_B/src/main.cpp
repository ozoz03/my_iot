#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ ПІНІВ
// ═══════════════════════════════════════════════════════════
#define LED_PIN 2  // GPIO2 — вбудований LED + зовнішній актуатор

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
#define MQTT_CLIENT_ID "esp32-ozoz03-b"              // унікальний — не як у ESP32-A!

#define TOPIC_BASE     "iot-course/ozoz03"
#define TOPIC_SENSORS  TOPIC_BASE "/sensors"          // підписка на дані з ESP32-A
#define TOPIC_COMMANDS TOPIC_BASE "/commands"         // підписка на команди ("manual_read")
#define SUBSCRIBE_QOS  1

// Таймер reconnect — чекаємо 5 секунд між спробами,
// максимум MQTT_MAX_RECONNECT_ATTEMPTS спроб поспіль
unsigned long lastReconnectAttempt = 0;
#define RECONNECT_INTERVAL       5000  // мс
#define MQTT_MAX_RECONNECT_ATTEMPTS 3
int  mqttReconnectAttempts  = 0;
bool mqttReconnectExhausted = false;

// ── Неблокуюче блимання LED (manual_read) ────────────────
#define BLINK_TOGGLE_COUNT 6      // 3 рази ON+OFF = 6 перемикань
#define BLINK_INTERVAL     200    // мс між перемиканнями
bool          blinkActive        = false;
int           blinkTogglesLeft   = 0;
unsigned long lastBlinkToggle    = 0;

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
// БЛИМАННЯ LED — старт неблокуючої послідовності 3х blink
// Сам blink виконується в loop(), не тут (callback без delay())
// ═══════════════════════════════════════════════════════════
void startBlink() {
    blinkActive      = true;
    blinkTogglesLeft = BLINK_TOGGLE_COUNT;
    lastBlinkToggle  = millis();
}

void handleBlink() {
    if (!blinkActive) return;

    unsigned long now = millis();
    if (now - lastBlinkToggle < BLINK_INTERVAL) return;

    lastBlinkToggle = now;
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    blinkTogglesLeft--;

    if (blinkTogglesLeft <= 0) {
        blinkActive = false;
        digitalWrite(LED_PIN, LOW);  // після blink повертаємось у вимкнений стан
    }
}

// ═══════════════════════════════════════════════════════════
// CALLBACK — викликається автоматично при вхідному повідомленні
// Викликається всередині mqttClient.loop()
// Не використовувати delay() і publish() всередині
// ═══════════════════════════════════════════════════════════
void onMessage(char* topic, byte* payload, unsigned int length) {
    // payload — масив байтів без термінатора '\0'
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.print("[MQTT] Топік: ");
    Serial.println(topic);
    Serial.print("[MQTT] Payload: ");
    Serial.println(message);

    if (strcmp(topic, TOPIC_COMMANDS) == 0) {
        if (strcmp(message, "manual_read") == 0) {
            Serial.println("Manual trigger received");
            startBlink();
        }
        return;
    }

    if (strcmp(topic, TOPIC_SENSORS) == 0) {
        // Простий парсинг JSON через strstr + atof
        // Без бібліотеки ArduinoJson — достатньо для відомої структури payload
        char* tempPtr = strstr(message, "\"temperature\":");
        if (tempPtr == NULL) {
            Serial.println("[MQTT] Температура не знайдена");
            return;
        }

        // Зсуваємось на 14 символів — довжина "temperature":
        float temperature = atof(tempPtr + 14);
        Serial.print("[MQTT] Температура: ");
        Serial.println(temperature);

        if (blinkActive) return;  // не перебивати активний manual_read blink

        if (temperature > 26.0) {
            digitalWrite(LED_PIN, HIGH);
            Serial.println("[LED] ON — вище 26°C");
        } else if (temperature < 20.0) {
            digitalWrite(LED_PIN, LOW);
            Serial.println("[LED] OFF — нижче 20°C");
        } else {
            Serial.println("[LED] Без змін — температура в нормі");
        }
    }
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

        // Підписуємось всередині connectMQTT — не в setup()
        // Бо Clean Session = true скидає підписки при кожному відключенні
        // Так підписка автоматично відновлюється після reconnect
        mqttClient.subscribe(TOPIC_SENSORS, SUBSCRIBE_QOS);
        mqttClient.subscribe(TOPIC_COMMANDS, SUBSCRIBE_QOS);
        Serial.print("[MQTT] Підписались на: ");
        Serial.print(TOPIC_SENSORS);
        Serial.print(" та ");
        Serial.println(TOPIC_COMMANDS);
        return true;
    }

    // mqttClient.state() повертає код помилки:
    // -4 = таймаут, -2 = сервер не знайдено, 5 = відмовлено в доступі
    Serial.print(" помилка: ");
    Serial.println(mqttClient.state());
    return false;
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // LED вимкнено при старті
    Serial.println("ESP32-B старт");

    connectWifi();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(onMessage);  // реєструємо callback до підключення
    mqttClient.setKeepAlive(60);        // PING кожні 60 секунд
    mqttClient.setSocketTimeout(30);    // таймаут TCP сокету 30 секунд
    if (connectMQTT()) {
        mqttReconnectAttempts = 0;
    }
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    handleBlink();

    if (mqttClient.connected()) {
        //  ОБОВ'ЯЗКОВО — без цього callback не викликається
        // і брокер не отримує PING → відключає клієнта
        mqttClient.loop();

        mqttReconnectAttempts  = 0;
        mqttReconnectExhausted = false;
    } else if (!mqttReconnectExhausted) {
        // millis() таймер між спробами reconnect
        // Не штурмуємо брокер — чекаємо RECONNECT_INTERVAL мс
        unsigned long now = millis();
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            mqttReconnectAttempts++;
            Serial.print("[MQTT] З'єднання втрачено — спроба ");
            Serial.print(mqttReconnectAttempts);
            Serial.print("/");
            Serial.println(MQTT_MAX_RECONNECT_ATTEMPTS);

            if (!connectMQTT() && mqttReconnectAttempts >= MQTT_MAX_RECONNECT_ATTEMPTS) {
                mqttReconnectExhausted = true;
                Serial.println("[MQTT] Досягнуто максимум спроб reconnect — припиняємо, поки Wi-Fi/брокер не відновляться");
            }
        }
    }
}
