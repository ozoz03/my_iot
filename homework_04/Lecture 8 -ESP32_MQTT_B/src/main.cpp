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
#define MQTT_BROKER    "broker.hivemq.com"       // публічний брокер HiveMQ
#define MQTT_PORT      1883                       // plain TCP, без TLS
#define MQTT_CLIENT_ID "esp32-demo-b"             // унікальний — не як у ESP32-A!
#define TOPIC_SENSORS  "iot-course/demo/sensors"  // топік на який підписуємось

// Таймер reconnect — чекаємо 5 секунд між спробами
// щоб не штурмувати брокер при нестабільному з'єднанні
unsigned long lastReconnectAttempt = 0;
#define RECONNECT_INTERVAL 5000  // мс

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
// CALLBACK — викликається автоматично при вхідному повідомленні
// Викликається всередині mqttClient.loop()
// Не використовувати delay() і publish() всередині
// ═══════════════════════════════════════════════════════════
void onMessage(char* topic, byte* payload, unsigned int length) {
    // payload — масив байтів без термінатора '\0'
    // треба самостійно перетворити в C-рядок
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';  // додаємо термінатор рядка

    Serial.print("[MQTT] Топік: ");
    Serial.println(topic);
    Serial.print("[MQTT] Payload: ");
    Serial.println(message);

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

    // Керуємо LED на основі температури
    // Замінити пороги 26.0 і 20.0 на власні значення
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
        mqttClient.subscribe(TOPIC_SENSORS, 1);
        Serial.print("[MQTT] Підписались на: ");
        Serial.println(TOPIC_SENSORS);
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
    connectMQTT();
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    if (mqttClient.connected()) {
        //  ОБОВ'ЯЗКОВО — без цього callback не викликається
        // і брокер не отримує PING → відключає клієнта
        mqttClient.loop();
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
