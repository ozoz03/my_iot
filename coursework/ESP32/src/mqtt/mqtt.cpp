#include "mqtt.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "../net/net.h"
#include "../secrets.h"

#define MQTT_PORT       8883                                  // MQTT over TLS, НЕ 1883!
#define TOPIC_TELEMETRY "iot-course/ozasymenko/sensors/data"  // вгору: публікуємо
#define TOPIC_CMD       "iot-course/ozasymenko/commands/led"        // вниз: слухаємо

#define RECONNECT_INTERVAL 5000  // мс між спробами

static WiFiClientSecure net;
static PubSubClient     mqttClient(net);

static unsigned long lastReconnectAttempt = 0;

// Чи вдалась остання синхронізація часу — якщо ні, TLS-handshake завжди
// падатиме (state -2), тому перед кожною спробою реконекту час треба
// синхронізувати повторно, а не лише сам mqtt_connect()
static bool timeOk = false;

// Пише callback, читає loop() — тому volatile на прапорці
static char          commandBuf[64];
static volatile bool commandReady = false;

// ═══════════════════════════════════════════════════════════
// CALLBACK — смикає його mqttClient.loop(), коли прийшло повідомлення
// Правило: скопіювати, підняти прапорець, вийти.
// ═══════════════════════════════════════════════════════════
static void onMessage(char* topic, byte* payload, unsigned int length) {
    // topic не розбираємо: підписка одна.
    // Зʼявиться commands/fan — роутинг по topic буде саме тут
    unsigned int n = length;

    // sizeof(commandBuf), а НЕ sizeof(payload):
    // payload — вказівник, sizeof від нього дасть 4
    if (n > sizeof(commandBuf) - 1) {
        n = sizeof(commandBuf) - 1;      // -1 — місце під '\0'
    }

    memcpy(commandBuf, payload, n);
    commandBuf[n] = '\0';                // payload прийшов без термінатора

    commandReady = true;                 // роботу зробить loop()
}

bool mqtt_begin() {
    // 1. Wi-Fi
    if (!net_wifi_connect()) {
        Serial.println("[AWS] Немає Wi-Fi — далі йти немає сенсу");
        return false;
    }

    // 2. Час — до сертифікатів, до connect(). Якщо не вийшло — не здаємось:
    // mqtt_reconnect_tick() повторить спробу перед кожним реконектом.
    timeOk = net_time_sync();

    // 3. Три файли з secrets.h у TLS-клієнт
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);

    // За замовчуванням TLS-хендшейк може мовчки чекати до 120с (без жодного
    // виводу в Serial) — виглядає як "зависання". Обмежуємо, щоб помилка
    // (невірний endpoint/сертифікат/час) проявлялась швидко.
    net.setTimeout(10);           // TCP connect + читання, секунди
    net.setHandshakeTimeout(15);  // TLS-хендшейк, секунди

    // 4. Брокер: наш AWS endpoint, порт 8883
    mqttClient.setServer(AWS_IOT_ENDPOINT, MQTT_PORT);

    // Буфер PubSubClient за замовчуванням 256 байт — замалий, мовчки обрізає
    // JSON. Один і той самий буфер обслуговує і вхід, і вихід.
    mqttClient.setBufferSize(512);

    // Callback ставимо ДО connect() — щоб не проґавити повідомлення
    mqttClient.setCallback(onMessage);

    mqttClient.setKeepAlive(60);        // PING кожні 60 секунд
    mqttClient.setSocketTimeout(30);    // таймаут TCP сокету 30 секунд

    return true;
}

// Client ID = THINGNAME — Policy обмежує Connect саме по ньому
bool mqtt_connect() {
    Serial.print("[MQTT] Підключаємось до AWS IoT Core...");

    if (mqttClient.connect(THINGNAME)) {
        Serial.println(" OK");

        // QoS 1: команда унікальна, повторити її нікому.
        // Телеметрію женемо QoS 0 — там наступний пакет перекриє втрачений
        if (mqttClient.subscribe(TOPIC_CMD, 1)) {
            Serial.println("[MQTT] Підписані на commands/led");
        } else {
            Serial.println("[MQTT] ПІДПИСКА НЕ ВДАЛАСЬ");
        }
        return true;
    }

    // mqttClient.state() коди: -2 = TLS/handshake помилка
    // (перевір час і endpoint), 5 = відмовлено (перевір Policy)
    Serial.print(" помилка: ");
    Serial.println(mqttClient.state());
    return false;
}

bool mqtt_connected() {
    return mqttClient.connected();
}

void mqtt_poll() {
    mqttClient.loop();
}

void mqtt_reconnect_tick() {
    unsigned long now = millis();
    if (now - lastReconnectAttempt <= RECONNECT_INTERVAL) return;

    lastReconnectAttempt = now;
    Serial.println("[MQTT] З'єднання втрачено — перепідключаємось...");

    // Wi-Fi міг відвалитись незалежно від MQTT — WiFi.status() завжди дає
    // актуальний стан, кешований прапорець тут би так само застарів,
    // як застарів timeOk без наступного фікса
    if (!net_wifi_connected()) {
        net_wifi_connect();
    }

    // Явно закриваємо стару TLS-сесію перед новою спробою —
    // інакше mbedTLS-контекст може лишитись "напівживим"
    net.stop();

    // Час міг не синхронізуватись при старті (UDP/123 не пройшов) —
    // без цього TLS-handshake завжди падатиме з state -2
    if (!timeOk) {
        timeOk = net_time_sync();
    }

    mqtt_connect();   // subscribe() всередині — підписка відновиться
}

const char* mqtt_take_command() {
    if (!commandReady) return NULL;
    commandReady = false;      // скидаємо ДО обробки
    return commandBuf;
}

// received_at тут НЕМА — його дописує правило в хмарі
void mqtt_publish_telemetry(const char* device_id, float temperature, float humidity, float lux) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не підключено — пропускаємо");
        return;
    }

    time_t now;
    time(&now);

    // snprintf замість String — безпечно для heap
    char payload[160];
    snprintf(payload, sizeof(payload),
        "{\"device_id\":\"%s\",\"timestamp\":%lu,\"temperature\":%.1f,\"humidity\":%.1f,\"lux\":%.1f}",
        device_id, (unsigned long)now, temperature, humidity, lux);

    Serial.print("[MQTT] Публікуємо: ");
    Serial.println(payload);

    bool ok = mqttClient.publish(TOPIC_TELEMETRY, payload);
    Serial.println(ok ? "[MQTT] OK" : "[MQTT] Помилка публікації");
}

void mqtt_publish(const char* topic, const char* payload) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не підключено — пропускаємо публікацію");
        return;
    }

    Serial.print("[MQTT] Публікуємо на ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(payload);

    bool ok = mqttClient.publish(topic, payload);
    Serial.println(ok ? "[MQTT] OK" : "[MQTT] Помилка публікації");
}
