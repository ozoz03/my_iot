#include "mqtt.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "../net/net.h"
#include "../secrets.h"
#include "../version.h"
#include "../shadow/shadow.h"

#define MQTT_PORT       8883                                  // MQTT over TLS, НЕ 1883!
#define TOPIC_TELEMETRY "iot-course/ozasymenko/sensors/data"  // вгору: публікуємо
#define TOPIC_CMD       "iot-course/ozasymenko/commands/led"        // вниз: слухаємо

// ═══ ТОПІКИ РОБІТ (OTA) ═══
// Зарезервовані топіки AWS — починаються з $aws, ми їх не вигадуємо.
// Складаємо через THINGNAME, бо в них зашите ім'я Thing.
// $next — службове слово «наступна робота в черзі», jobId знати не треба.
#define TOPIC_JOBS_NOTIFY  "$aws/things/" THINGNAME "/jobs/notify-next"
#define TOPIC_JOBS_GET     "$aws/things/" THINGNAME "/jobs/$next/get"
#define TOPIC_JOBS_ACCEPT  "$aws/things/" THINGNAME "/jobs/$next/get/accepted"

// Експоненційний backoff між спробами реконекту: 1с, 2с, 4с, 8с, 16с, стеля 30с.
// Після вдалого конекту інтервал скидається назад до мінімуму.
#define RECONNECT_MIN 1000UL
#define RECONNECT_MAX 30000UL

static WiFiClientSecure net;
static PubSubClient     mqttClient(net);

static unsigned long lastReconnectAttempt = 0;
static unsigned long reconnectInterval = RECONNECT_MIN;

// Чи вдалась остання синхронізація часу — якщо ні, TLS-handshake завжди
// падатиме (state -2), тому перед кожною спробою реконекту час треба
// синхронізувати повторно, а не лише сам mqtt_connect()
static bool timeOk = false;

// Команда: пише callback, читає loop() — тому volatile на прапорці
static char          commandBuf[64];
static volatile bool commandReady = false;

// Робота: окремий буфер, ВЕЛИКИЙ. Документ з presigned URL — це не
// 256 байт. 3 КБ із запасом.
static char          jobBuf[3072];
static volatile bool jobReady = false;

// ═══════════════════════════════════════════════════════════
// CALLBACK — диспетчер на три види топіка (команда / notify / документ
// роботи). Правило те саме: скопіювати, підняти прапорець, вийти.
// Жодної роботи тут: буфер PubSubClient спільний на вхід і вихід,
// а поки ми в callback — не летить PING і брокер рахує нас мертвими.
// Завантаження прошивки тим паче тут робити НЕ МОЖНА — це десятки секунд.
// ═══════════════════════════════════════════════════════════
static void onMessage(char* topic, byte* payload, unsigned int length) {

    // ── Тінь ──
    // Віддаємо в свій модуль. Він поверне true, якщо топік був його.
    if (shadow_on_message(topic, payload, length)) {
        return;
    }

    // ── Команда на LED ──
    if (strstr(topic, "/commands/led") != NULL) {
        unsigned int n = length;
        // sizeof(commandBuf), а НЕ sizeof(payload):
        // payload — вказівник, sizeof від нього дасть 4
        if (n > sizeof(commandBuf) - 1) {
            n = sizeof(commandBuf) - 1;      // -1 — місце під '\0'
        }
        memcpy(commandBuf, payload, n);
        commandBuf[n] = '\0';                // payload прийшов без термінатора
        commandReady = true;                 // роботу зробить loop()
        return;
    }

    // ── Сповіщення «є нова робота» ──
    // Нічого не копіюємо: одразу просимо повний документ.
    // publish із callback — коротка операція, це прийнятно.
    if (strstr(topic, "/jobs/notify-next") != NULL) {
        mqttClient.publish(TOPIC_JOBS_GET, "{}");
        return;
    }

    // ── Приїхав документ роботи ──
    if (strstr(topic, "/jobs/$next/get/accepted") != NULL) {
        if (length >= sizeof(jobBuf)) return;   // не вліз — ігноруємо
        memcpy(jobBuf, payload, length);
        jobBuf[length] = '\0';
        jobReady = true;                        // роботу зробить loop() → ota
        return;
    }
}

bool mqtt_begin() {
    // 1. Wi-Fi
    if (!net_wifi_connect()) {
        Serial.println("[AWS] Немає Wi-Fi — далі йти немає сенсу");
        return false;
    }

    // 2. Час — до сертифікатів, до connect(). Якщо не вийшло — не падаємо
    // тут: mqtt_connect() просто поверне false, work_cycle() піде спати,
    // а наступне пробудження викличе mqtt_begin() заново з нуля.
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

    // 4. Вказуємо брокер: наш AWS endpoint, порт 8883
    mqttClient.setServer(AWS_IOT_ENDPOINT, MQTT_PORT);

    // Буфер підняли з 512 до 3072 — документ роботи з presigned URL у 512
    // НЕ ВЛІЗЕ, обріжеться МОВЧКИ, а ArduinoJson поверне IncompleteInput
    // і виглядатиме як баг парсера.
    mqttClient.setBufferSize(3072);

    // Callback ставимо ДО connect() — щоб не проґавити повідомлення
    mqttClient.setCallback(onMessage);

    mqttClient.setKeepAlive(60);        // PING кожні 60 секунд
    mqttClient.setSocketTimeout(30);    // таймаут TCP сокету 30 секунд

    return true;
}

// Client ID = THINGNAME — Policy обмежує Connect саме по ньому
bool mqtt_connect() {
    Serial.print("[MQTT] Підключаємось до AWS IoT Core...");

    if (!mqttClient.connect(THINGNAME)) {
        // mqttClient.state() коди: -2 = TLS/handshake помилка
        // (перевір час і endpoint), 5 = відмовлено (перевір Policy)
        Serial.print(" помилка: ");
        Serial.println(mqttClient.state());
        return false;
    }

    Serial.println(" OK");

    // QoS 1: команда унікальна, повторити її нікому.
    // Телеметрію женемо QoS 0 — там наступний пакет перекриє втрачений
    if (mqttClient.subscribe(TOPIC_CMD, 1)) {
        Serial.println("[MQTT] Підписані на commands/led");
    } else {
        Serial.println("[MQTT] ПІДПИСКА commands НЕ ВДАЛАСЬ");
    }

    // ── Підписки на роботи (OTA) ──
    // Всі підписки ТУТ, а не в mqtt_begin(): реконект = нова сесія =
    // нуль підписок, тож вони мають відновлюватись разом з кожним connect().
    mqttClient.subscribe(TOPIC_JOBS_NOTIFY, 1);
    mqttClient.subscribe(TOPIC_JOBS_ACCEPT, 1);
    Serial.println("[MQTT] Підписані на jobs");

    // ── Підписки на тінь ──
    // Перелічуємо ЯВНО. Не shadow/# — AWS час від часу додає нові топіки
    // в цю структуру, і решітка почне тягнути те, чого ми не просили,
    // а кожне зайве повідомлення — це радіо.
    // ЯКЩО ТУТ МОВЧАННЯ: перевір Policy. Без shadow-топіків у політиці
    // підписка «вдається», але дельта не приходить ніколи. Помилки не буде.
    mqttClient.subscribe(TOPIC_SHADOW_DELTA, 1);
    mqttClient.subscribe(TOPIC_SHADOW_GET_ACC, 1);

    // Відповідь на НАШ запис у shadow/update. Без цих двох звіт летить
    // у порожнечу: ми не дізнаємось ні що reported оновлено, ні що
    // документ відхилено — і дельта повертатиметься щопробудження.
    mqttClient.subscribe(TOPIC_SHADOW_UPD_ACC, 1);
    mqttClient.subscribe(TOPIC_SHADOW_UPD_REJ, 1);
    Serial.println("[MQTT] Підписані на shadow");

    // ── КРИТИЧНО: одразу питаємо, чи є робота ──
    // Поки нас не було, роботу могли створити. notify-next тоді не прилетить —
    // її треба забрати самим. Без цього рядка пропустимо оновлення після
    // перезавантаження/втрати зв'язку.
    mqtt_request_next_job();

    return true;
}

bool mqtt_connected() {
    return mqttClient.connected();
}

void mqtt_poll() {
    mqttClient.loop();
}

void mqtt_reconnect_tick() {
    unsigned long now = millis();
    if (now - lastReconnectAttempt <= reconnectInterval) return;

    lastReconnectAttempt = now;
    Serial.print("[MQTT] З'єднання втрачено — перепідключаємось (інтервал ");
    Serial.print(reconnectInterval / 1000);
    Serial.println("с)...");

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

    if (mqtt_connect()) {   // subscribe() всередині — підписка відновиться
        // Вдалось — наступного разу, коли з'єднання знову впаде,
        // відлік backoff починається заново з мінімуму
        reconnectInterval = RECONNECT_MIN;
    } else {
        // Експоненційне зростання з стелею: 1с → 2с → 4с → ... → 30с
        reconnectInterval = min(reconnectInterval * 2, RECONNECT_MAX);
    }
}

const char* mqtt_take_command() {
    if (!commandReady) return NULL;
    commandReady = false;      // скидаємо ДО обробки
    return commandBuf;
}

// Віддаємо сирий документ. Парсить ota.cpp.
const char* mqtt_take_job() {
    if (!jobReady) return NULL;
    jobReady = false;
    return jobBuf;
}

// Вихід достроковий, і це не косметика: дві секунди чекання на ~100 мА
// коштують дорожче, ніж хвилина сну. Відповідь зазвичай приходить за
// 200-400 мс.
bool mqtt_wait_for_job(unsigned long timeoutMs) {
    unsigned long start = millis();

    while (millis() - start < timeoutMs) {
        mqttClient.loop();          // ОСЬ ЗАРАДИ ЧОГО ВСЕ

        if (jobReady) {
            Serial.print("[JOBS] Відповідь за ");
            Serial.print(millis() - start);
            Serial.println(" мс");
            return true;            // прийшло — не палимо радіо далі
        }

        delay(10);                  // не молотимо CPU даремно
    }

    Serial.println("[JOBS] Таймаут — робіт у черзі немає");
    return false;
}

void mqtt_disconnect_for_ota() {
    mqttClient.disconnect();
    net.stop();   // саме net.stop() — інакше контекст mbedTLS лишиться в купі
    Serial.println("[MQTT] З'єднання закрито на час OTA");
}

void mqtt_request_next_job() {
    // Порожнє тіло: сам факт публікації в get — це і є запит
    mqttClient.publish(TOPIC_JOBS_GET, "{}");
}

void mqtt_report_status(const char* jobId, const char* status) {
    // Топік звіту містить jobId усередині себе — ось навіщо ota його зберігає
    char topic[160];
    snprintf(topic, sizeof(topic),
             "$aws/things/" THINGNAME "/jobs/%s/update", jobId);

    char payload[64];
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", status);

    mqttClient.publish(topic, payload);
    Serial.print("[JOBS] Статус ");
    Serial.print(status);
    Serial.print(" для ");
    Serial.println(jobId);
}

// received_at тут НЕМА — його дописує правило в хмарі.
// firmware_version — щоб у хмарі було видно, хто на якій версії; після
// OTA це єдиний доказ, що оновлення справді відбулось.
void mqtt_publish_telemetry(const char* device_id, float temperature, float humidity, float lux) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не підключено — пропускаємо");
        return;
    }

    time_t now;
    time(&now);

    // snprintf замість String — безпечно для heap
    char payload[200];
    snprintf(payload, sizeof(payload),
        "{\"device_id\":\"%s\",\"timestamp\":%lu,\"temperature\":%.1f,\"humidity\":%.1f,"
        "\"lux\":%.1f,\"firmware_version\":\"%s\"}",
        device_id, (unsigned long)now, temperature, humidity, lux, FIRMWARE_VERSION);

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
