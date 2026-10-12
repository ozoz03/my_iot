#include "ota.h"
#include <WiFi.h>              // WiFiClient — у ядрі 3.x WiFiClientSecure.h його більше не підтягує
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>
#include "../mqtt/mqtt.h"
#include "../version.h"

// Менша за це — точно не прошивка (HTML-сторінка помилки, редірект).
// Реальний образ важить сотні КБ.
#define MIN_FIRMWARE_SIZE 100000

volatile bool otaInProgress = false;

// ═══════════════════════════════════════════════════════════
// ВІДКРИТИ ПОТІК З ПОЗИЦІЇ from.
// from = 0 — звичайний GET, чекаємо 200.
// from > 0 — докачка: просимо лише хвіст заголовком Range, чекаємо 206.
// Повертає, скільки байтів сервер збирається віддати, або -1.
// ═══════════════════════════════════════════════════════════
static int open_stream(HTTPClient& http, WiFiClientSecure& client,
                       const char* url, size_t from) {
    http.begin(client, url);

    // ═══ КРИТИЧНО: йти по редіректах ═══
    // S3 і presigned URL часто відповідають 301/302 на регіональний хост.
    // Без цього HTTPClient зупиняється на редіректі → потік порожній → 0 байтів.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int expected = HTTP_CODE_OK;
    if (from > 0) {
        char range[32];
        snprintf(range, sizeof(range), "bytes=%u-", (unsigned)from);
        http.addHeader("Range", range);
        expected = HTTP_CODE_PARTIAL_CONTENT;
    }

    int code = http.GET();
    Serial.print("[OTA] HTTP GET -> ");
    Serial.println(code);

    if (code != expected) {
        // 403 — presigned невалідний (адреса/роль/протух)
        // 301/302 — редірект не відпрацював
        // 404 — не той шлях / приватне репо
        // 200 замість 206 — сервер проігнорував Range, докачка неможлива
        http.end();
        return -1;
    }
    return http.getSize();
}

// ═══════════════════════════════════════════════════════════
// ПЕРЕКАЧАТИ ПОТІК У Update, поки не доберемо total
// або поки з'єднання не обірветься. Повертає нове значення written.
//
// РУЧНЕ ЗАВАНТАЖЕННЯ ШМАТКАМИ по 4 КБ — замість Update.writeStream().
// Чому не writeStream: у arduino-esp32 (issue #9997) writeStream
// помилково завершується при read()==0 посеред потоку. Великий TLS-потік
// з S3 віддається нерівними шматками — read() тимчасово повертає 0,
// і writeStream вирішує, що потік мертвий → "Записано 0".
// Ручний цикл з чеканням на дані обходить це (PR #11865 радить те саме).
// ═══════════════════════════════════════════════════════════
static size_t pump_stream(WiFiClientSecure& client, WiFiClient* stream,
                          size_t written, size_t total) {
    const size_t CHUNK = 4096;
    static uint8_t buf[CHUNK];

    // Скільки поспіль разів прийшов нуль байтів. Нуль ≠ кінець потоку:
    // TLS може тимчасово не мати даних, поки connected(). Кінець — це
    // або добрали total, або з'єднання реально закрилось.
    int emptyReads = 0;
    const int MAX_EMPTY_READS = 200;   // ~200×50мс = 10с без даних = здаємось

    while (written < total) {

        // НЕ yield()! На ESP32 yield() перемикає лише задачі того ж
        // пріоритету — IDLE (пріоритет 0) так і не отримає час і не
        // погодує Task WDT. delay(1) блокує loopTask на тік → IDLE
        // прокидається → watchdog нагодований.
        delay(1);

        size_t avail = stream->available();

        if (avail == 0) {
            // Даних поки немає. Живі? — чекаємо. Мертві й недобрали? — виходимо.
            if (!client.connected() && stream->available() == 0) {
                Serial.println("[OTA] З'єднання закрилось до кінця файлу");
                break;
            }
            if (++emptyReads > MAX_EMPTY_READS) {
                Serial.println("[OTA] Потік завис — даних немає надто довго");
                break;
            }
            delay(50);
            continue;
        }

        emptyReads = 0;   // дані пішли — скидаємо лічильник тиші

        size_t toRead = avail < CHUNK ? avail : CHUNK;
        int got = stream->readBytes(buf, toRead);
        if (got <= 0) {
            delay(50);
            continue;
        }

        // Пишемо саме стільки, скільки прочитали
        size_t w = Update.write(buf, got);
        if (w != (size_t)got) {
            Serial.println("[OTA] Update.write не прийняв усі байти");
            Update.printError(Serial);
            break;   // written не доросте до total — download_and_flash зробить abort
        }

        written += w;
    }
    return written;
}

// ═══════════════════════════════════════════════════════════
// ЗАВАНТАЖЕННЯ + ЗАПИС.
// Потік із HTTPS-відповіді йде прямо в Update — без буферизації в RAM.
// Мегабайт проходить крізь пристрій, ніколи в ньому не поміщаючись.
//
// ═══ ДОКАЧКА ═══
// У ядрі 3.x NetworkClientSecure::available() при помилці читання
// (-76, сервер закрив з'єднання) робить stop() — і викидає байти, які
// вже прийшли й лежать у буфері TLS. Файл обривається за кілька КБ до
// кінця, щоразу на тому самому місці. Замість того щоб викидати мегабайт,
// відкриваємо з'єднання ще раз і просимо лише хвіст: Range: bytes=N-.
// Update-сесія при цьому не закривається — пишемо далі в той самий слот.
// ═══════════════════════════════════════════════════════════
#define MAX_RESUMES 3

static bool download_and_flash(const char* url) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    int len = open_stream(http, client, url, 0);
    if (len < 0) return false;

    Serial.print("[OTA] Content-Length: ");
    Serial.println(len);

    // Захист від сміття замість прошивки
    if (len < MIN_FIRMWARE_SIZE) {
        Serial.print("[OTA] Замало байтів: ");
        Serial.println(len);
        http.end();
        return false;
    }

    // Резервуємо місце у вільному слоті. Слот вибирає бібліотека.
    if (!Update.begin(len)) {
        Update.printError(Serial);   // тут вилізе "розділ не знайдено"
        http.end();
        return false;
    }

    Serial.println("[OTA] Пишемо прошивку...");
    size_t written = pump_stream(client, http.getStreamPtr(), 0, len);

    for (int attempt = 1; written < (size_t)len && attempt <= MAX_RESUMES; attempt++) {
        if (Update.hasError()) break;   // флеш відмовив — докачка не допоможе
        http.end();

        Serial.print("[OTA] Обрив на ");
        Serial.print(written);
        Serial.print(" з ");
        Serial.print(len);
        Serial.print(" — докачуємо, спроба ");
        Serial.println(attempt);

        // Сервер має віддати рівно хвіст. Інакше — не той файл або
        // Range проігноровано: писати таке в слот не можна.
        int rest = open_stream(http, client, url, written);
        if (rest != (int)(len - written)) {
            Serial.println("[OTA] Сервер віддав не той хвіст — здаємось");
            break;
        }
        written = pump_stream(client, http.getStreamPtr(), written, len);
    }

    if (written != (size_t)len) {
        Serial.print("[OTA] Записано лише ");
        Serial.print(written);
        Serial.print(" з ");
        Serial.println(len);
        Update.abort();              // звільняємо слот, otadata не чіпаємо
        http.end();
        return false;
    }

    Serial.print("[OTA] Завантажено байтів: ");
    Serial.println(written);

    // Перевірка образу і — тільки після неї — запис у otadata
    if (!Update.end(true)) {
        Update.printError(Serial);
        http.end();
        return false;
    }

    http.end();
    return true;
}

// ═══════════════════════════════════════════════════════════
// ОБРОБКА ДОКУМЕНТА РОБОТИ.
// Викликається з loop(), коли mqtt_take_job() віддав документ.
// ═══════════════════════════════════════════════════════════
void ota_handle_job(const char* jobDocument) {

    // ── Розбір ──
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jobDocument);
    if (err) {
        Serial.print("[OTA] JSON: ");
        Serial.println(err.c_str());   // IncompleteInput = замалий буфер MQTT
        return;
    }

    // Роботи немає — поля execution просто не буде. Це нормальна відповідь.
    if (!doc["execution"].is<JsonObject>()) {
        Serial.println("[OTA] Робіт у черзі немає");
        return;
    }

    const char* jobId   = doc["execution"]["jobId"];
    const char* version = doc["execution"]["jobDocument"]["version"];
    const char* url     = doc["execution"]["jobDocument"]["url"];

    if (!jobId || !version || !url) {
        Serial.println("[OTA] Документ без jobId/version/url — пропускаємо");
        return;
    }

    // ── Я ВЖЕ НА ЦІЙ ВЕРСІЇ? ──
    // Значить, я щойно оновився і питаю ту саму роботу, що висить IN_PROGRESS.
    // Звітую SUCCEEDED — і закриваю її. Без цієї перевірки нова прошивка
    // почала б качати саму себе по колу.
    if (strcmp(version, FIRMWARE_VERSION) == 0) {
        Serial.println("[OTA] Я вже на цій версії — робота виконана");
        mqtt_report_status(jobId, "SUCCEEDED");
        return;
    }

    // ── Оновлюємось ──
    Serial.print("[OTA] Нова версія ");
    Serial.print(version);
    Serial.print(" (у мене ");
    Serial.print(FIRMWARE_VERSION);
    Serial.println(") — починаю");

    otaInProgress = true;

    // Остання можливість щось сказати перед розривом звʼязку
    mqtt_report_status(jobId, "IN_PROGRESS");

    // Рвемо MQTT: звільняємо TLS-контекст під HTTPS і знімаємо питання KeepAlive
    mqtt_disconnect_for_ota();

    // ═══ ПОДОВЖУЄМО WATCHDOG НА ЧАС ЗАПИСУ ═══
    // Запис ~1 МБ у флеш + перевірка SHA у Update.end() довші за звичайні
    // 5 с watchdog. OTA — легітимна довга операція, тож на її час даємо 60 с.
    //
    // esp_task_wdt_reconfigure() існує лише в ESP-IDF 5.x (ядро arduino-esp32
    // 3.x). У встановленому тут ядрі (ESP-IDF 4.4) такої функції немає —
    // за документацією esp_task_wdt.h повторний esp_task_wdt_init() на вже
    // ініціалізованому TWDT робить те саме: оновлює timeout і panic, а не
    // створює нову підписку. Параметр тут — секунди, а не мілісекунди.
    esp_task_wdt_init(60, true);

    // Довга фаза. MQTT тут не існує. Пристрій зайнятий єдиною справою.
    bool ok = download_and_flash(url);

    // Повертаємо стандартний таймаут зі sdkconfig
    esp_task_wdt_init(CONFIG_ESP_TASK_WDT_TIMEOUT_S, true);


    if (ok) {
        Serial.println("[OTA] Готово. Перезавантаження...");
        Serial.flush();
        delay(500);

        // ═══ ЧОМУ НЕ ESP.restart() ═══
        // У Wokwi нова прошивка після ESP.restart() інколи зависала на старті,
        // ще до setup(). Образ тоді ще не підтверджено — завантажувач
        // позначає слот ABORTED і повертає нас на стару версію.
        // Пробудження з deep sleep скидає процесори й цифрову периферію —
        // з ним нова прошивка стартує стабільно. RTC-памʼять при цьому
        // ЖИВЕ: змінні RTC_DATA_ATTR нова прошивка отримає від старої.
        // Це обхід специфіки Wokwi; на реальній платі ESP.restart() —
        // стандартний спосіб завершити OTA. 100 мс сну коштують нічого.
        esp_sleep_enable_timer_wakeup(100000);   // 100 мс
        esp_deep_sleep_start();
    }

    // ── Тільки при невдачі ──
    // Слот засмічений, otadata не чіпали — стартує стара прошивка.
    // Піднімаємо MQTT назад і зізнаємось.
    Serial.println("[OTA] Не вдалось. Працюємо на старій версії");
    otaInProgress = false;
    mqtt_connect();                      // підписки + запит роботи всередині
    mqtt_report_status(jobId, "FAILED");
}
