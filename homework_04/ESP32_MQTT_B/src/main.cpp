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

#define TOPIC_BASE          "iot-course/ozoz03"
#define TOPIC_SENSOR_TEMP   TOPIC_BASE "/sensors/temperature"  // підписка — лише температура, без JSON
#define TOPIC_COMMANDS      TOPIC_BASE "/commands"             // підписка на команди ("manual_read")
#define TOPIC_ACTUATOR_LED  TOPIC_BASE "/actuators/led"        // публікуємо поточний стан LED (retained)
#define TOPIC_STATUS        TOPIC_BASE "/status/esp32-b"       // online/offline (retained, LWT)
#define SUBSCRIBE_QOS  1

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

// ── Неблокуюче блимання LED (manual_read) ────────────────
#define BLINK_TOGGLE_COUNT 6      // 3 рази ON+OFF = 6 перемикань
#define BLINK_INTERVAL     200    // мс між перемиканнями
bool          blinkActive         = false;
int           blinkTogglesLeft    = 0;
unsigned long lastBlinkToggle     = 0;
bool          blinkLevel          = false;  // програмний рівень blink-послідовності (не читаємо пін!)

// Логічний стан, якому LED має відповідати поза blink — виставляється
// температурними порогами в onMessage() і застосовується до піна одразу,
// якщо blink не активний, або після завершення blink (замість жорсткого LOW).
// Оновлюється навіть коли blink триває — щоб після нього LED відповідав
// останній відомій температурі, а не знімку на момент старту blink.
bool          ledStateBeforeBlink = false;

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
    ledStateBeforeBlink = digitalRead(LED_PIN);  // знімок стану на старті (безпечний дефолт)
    blinkActive      = true;
    blinkTogglesLeft = BLINK_TOGGLE_COUNT;
    blinkLevel       = false;  // послідовність завжди стартує з LOW→HIGH, незалежно від поточного стану LED
    lastBlinkToggle  = millis();
}

void handleBlink() {
    if (!blinkActive) return;

    unsigned long now = millis();
    if (now - lastBlinkToggle < BLINK_INTERVAL) return;

    lastBlinkToggle = now;
    blinkLevel = !blinkLevel;
    digitalWrite(LED_PIN, blinkLevel);
    blinkTogglesLeft--;

    if (blinkTogglesLeft <= 0) {
        blinkActive = false;
        // Повертаємось не до жорсткого LOW, а до стану за останньою температурою
        // (він міг оновитись і під час самого blink — див. onMessage())
        digitalWrite(LED_PIN, ledStateBeforeBlink);
    }
}

// ═══════════════════════════════════════════════════════════
// CALLBACK — викликається автоматично при вхідному повідомленні
// Викликається всередині mqttClient.loop()
// Не використовувати delay() і publish() всередині
// ═══════════════════════════════════════════════════════════
void onMessage(char* topic, byte* payload, unsigned int length) {
    // Фіксований буфер замість char message[length + 1] на стеку — payload
    // приходить від будь-кого на публічному брокері, а length ми не контролюємо.
    // Із поточним MQTT_MAX_PACKET_SIZE (256, PubSubClient.h) переповнення й так
    // не було б, але якщо колись підняти setBufferSize(), VLA стане реальним
    // ризиком для стеку loopTask. static — щоб не займати стек callback'у зайвим.
    static char message[128];
    if (length >= sizeof(message)) {
        Serial.println("[MQTT] Payload задовгий — ігноруємо");
        return;
    }
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

    if (strcmp(topic, TOPIC_SENSOR_TEMP) == 0) {
        // Топік містить лише значення температури — розбір JSON не потрібен
        float temperature = atof(message);
        Serial.print("[MQTT] Температура: ");
        Serial.println(temperature);

        // Оновлюємо цільовий стан LED за порогами ЗАВЖДИ — навіть коли триває
        // manual_read blink. Так після завершення блимання LED відповідає
        // останній відомій температурі, а не знімку стану на старті blink.
        if (temperature > 26.0) {
            ledStateBeforeBlink = true;
        } else if (temperature < 20.0) {
            ledStateBeforeBlink = false;
        }
        // між 20°C і 26°C — мертва зона, ledStateBeforeBlink лишається без змін

        if (blinkActive) return;  // сам пін під час blink не чіпаємо — це зона handleBlink()

        bool ledOn = digitalRead(LED_PIN);
        if (ledStateBeforeBlink != ledOn) {
            digitalWrite(LED_PIN, ledStateBeforeBlink);
            mqttClient.publish(TOPIC_ACTUATOR_LED, ledStateBeforeBlink ? "ON" : "OFF", true);  // retained
            Serial.println(ledStateBeforeBlink ? "[LED] ON — вище 26°C" : "[LED] OFF — нижче 20°C");
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

    // LWT: якщо з'єднання обірветься нештатно, брокер сам опублікує
    // "offline" (retained) у TOPIC_STATUS
    if (mqttClient.connect(MQTT_CLIENT_ID, TOPIC_STATUS, 1, true, "offline")) {
        Serial.println(" OK");
        mqttClient.publish(TOPIC_STATUS, "online", true);  // retained

        // Підписуємось всередині connectMQTT — не в setup()
        // Бо Clean Session = true скидає підписки при кожному відключенні
        // Так підписка автоматично відновлюється після reconnect
        mqttClient.subscribe(TOPIC_SENSOR_TEMP, SUBSCRIBE_QOS);
        mqttClient.subscribe(TOPIC_COMMANDS, SUBSCRIBE_QOS);
        Serial.print("[MQTT] Підписались на: ");
        Serial.print(TOPIC_SENSOR_TEMP);
        Serial.print(" та ");
        Serial.println(TOPIC_COMMANDS);

        // Стартовий стан актуатора — LED вимкнено при старті (setup())
        mqttClient.publish(TOPIC_ACTUATOR_LED, "OFF", true);  // retained
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

    bool wifiOk = connectWifi();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(onMessage);  // реєструємо callback до підключення
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
    handleBlink();

    if (mqttClient.connected()) {
        //  ОБОВ'ЯЗКОВО — без цього callback не викликається
        // і брокер не отримує PING → відключає клієнта
        mqttClient.loop();

        mqttReconnectAttempts  = 0;
        mqttReconnectExhausted = false;
    } else {
        maintainConnection();
    }
}
