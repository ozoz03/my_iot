#include "shadow.h"
#include <ArduinoJson.h>
#include "../mqtt/mqtt.h"

// Документ із get/accepted більший за дельту: крім state там їде
// metadata з мітками часу на КОЖНЕ поле. 2 КБ вистачає для нашої
// тіні з двох полів. Не вліз — ігноруємо, як і з документом роботи.
static char          shadowBuf[2048];
static volatile bool shadowReady  = false;

// З якого топіка прийшло. Від цього залежить, ДЕ всередині JSON
// лежить різниця:
//   update/delta  → корінь state          (AWS уже дав тільки різницю)
//   get/accepted  → state.delta           (повний документ, різниця всередині)
static volatile bool fromGetAccepted = false;

// Відповідь AWS на наш запис у shadow/update.
// rejectBuf окремий і маленький: тіло відмови — це {"code":403,...},
// і воно не має права затерти документ, який ми ще розбираємо.
static volatile bool updateAccepted = false;
static volatile bool updateRejected = false;
static char          rejectBuf[160];

// ═══════════════════════════════════════════════════════════
// ПРИЙОМ. Викликається з callback — тому тільки копіюємо і виходимо.
// Парсити тут не можна: буфер PubSubClient спільний на вхід і вихід,
// а поки ми в callback — не летить PING.
// ═══════════════════════════════════════════════════════════
bool shadow_on_message(char* topic, byte* payload, unsigned int length) {

    // ── Відповідь на НАШ запис ──
    // Ловимо першими: тіло сюди не копіюємо в спільний shadowBuf.
    if (strstr(topic, "/shadow/update/accepted") != NULL) {
        updateAccepted = true;
        return true;
    }

    if (strstr(topic, "/shadow/update/rejected") != NULL) {
        unsigned int n = length;
        if (n > sizeof(rejectBuf) - 1) n = sizeof(rejectBuf) - 1;
        memcpy(rejectBuf, payload, n);
        rejectBuf[n] = '\0';
        updateRejected = true;
        return true;
    }

    bool isDelta = (strstr(topic, "/shadow/update/delta") != NULL);
    bool isGet   = (strstr(topic, "/shadow/get/accepted") != NULL);

    if (!isDelta && !isGet) {
        return false;   // не наше — хай розбирається mqtt.cpp далі
    }

    if (length >= sizeof(shadowBuf)) {
        Serial.println("[SHADOW] Документ не влазить у буфер");
        return true;    // топік наш, але забрати не змогли
    }

    memcpy(shadowBuf, payload, length);
    shadowBuf[length] = '\0';       // payload приходить без термінатора

    fromGetAccepted = isGet;
    shadowReady     = true;         // роботу зробить work_cycle()

    return true;
}

// ═══════════════════════════════════════════════════════════
// ЗАПИТ. Порожнє тіло: сам факт публікації в /get і є запитом.
// Точно як $next/get для робіт.
// ═══════════════════════════════════════════════════════════
void shadow_request() {
    mqtt_publish(TOPIC_SHADOW_GET, "{}");
    Serial.println("[SHADOW] Запитали документ");
}

// ═══════════════════════════════════════════════════════════
// АКТИВНЕ ОЧІКУВАННЯ.
// НЕ можна просто покластись на вікно mqtt_wait_for_job(): та функція
// виходить одразу, щойно прийде відповідь про роботу (jobReady), а
// робота майже завжди відповідає за ~150мс — набагато швидше, ніж
// типова відповідь тіні. Спільне вікно на практиці означало б «майже
// ніколи не встигаємо», а не «іноді не встигаємо». Тому чекаємо окремо,
// але коротше за старі 2000мс — реальні відповіді AWS, які ми бачили,
// йдуть за 150-250мс.
// ═══════════════════════════════════════════════════════════
bool shadow_wait_for_delta(unsigned long timeoutMs) {
    unsigned long start = millis();

    while (millis() - start < timeoutMs) {
        mqtt_poll();

        if (shadowReady) {
            Serial.print("[SHADOW] Відповідь за ");
            Serial.print(millis() - start);
            Serial.println(" мс");
            return true;
        }

        delay(10);                  // не молотимо CPU даремно
    }

    // Не помилка. Найчастіше означає: desired і reported збігаються,
    // тому AWS не мав чого надсилати. Тиша = «стан уже правильний».
    Serial.println("[SHADOW] Тиша — стан уже правильний");
    return false;
}

// ═══════════════════════════════════════════════════════════
// РОЗБІР. Викликається вже поза callback — тут парсити можна.
// ═══════════════════════════════════════════════════════════
bool shadow_take_delta(ShadowDelta* out) {
    if (!shadowReady) return false;
    shadowReady = false;            // скидаємо ДО обробки

    out->hasIndicator = false;
    out->hasInterval = false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, shadowBuf);
    if (err) {
        Serial.print("[SHADOW] JSON: ");
        Serial.println(err.c_str());   // IncompleteInput = замалий буфер MQTT
        return false;
    }

    // ── Три окремі логи — desired / reported / delta ──
    // Друкуємо БЕЗУМОВНО, ще до перевірки нижче — корисно бачити навіть
    // коли desired і reported збігаються (нормальний «нічого не
    // змінилось» цикл). get/accepted несе всі три; update/delta — лише
    // саму дельту (AWS уже поклав її в корінь state, окремого
    // desired/reported там немає).
    //
    // serializeJsonPretty, а не серіалізація в один рядок, і порожній
    // рядок до/після — суто щоб відповідь було добре видно й легко
    // відрізнити одну від одної на весь екран Serial Monitor-а, а не
    // шукати очима в стрічці компактного JSON.
    Serial.println();
    Serial.println("――――――――――――――――――――――――――――――――――――――――――――――――――");
    if (fromGetAccepted) {
        Serial.println("[SHADOW] desired:");
        serializeJsonPretty(doc["state"]["desired"], Serial);
        Serial.println();

        Serial.println("[SHADOW] reported:");
        serializeJsonPretty(doc["state"]["reported"], Serial);
        Serial.println();

        Serial.println("[SHADOW] delta:");
        if (doc["state"]["delta"].is<JsonObject>()) {
            serializeJsonPretty(doc["state"]["delta"], Serial);
            Serial.println();
        } else {
            Serial.println("  (немає — desired і reported уже збігаються)");
        }
    } else {
        Serial.println("[SHADOW] delta:");
        serializeJsonPretty(doc["state"], Serial);
        Serial.println();
    }
    Serial.println("――――――――――――――――――――――――――――――――――――――――――――――――――");
    Serial.println();

    // ── Знаходимо, де лежить різниця ──
    JsonObject delta;

    if (fromGetAccepted) {
        // Повний документ. Поля delta НЕМАЄ ВЗАГАЛІ, якщо desired
        // і reported збігаються — це нормальна відповідь «все правильно».
        if (!doc["state"]["delta"].is<JsonObject>()) {
            Serial.println("[SHADOW] Різниці немає — стан уже правильний");
            return false;
        }
        delta = doc["state"]["delta"];
    } else {
        // update/delta: AWS уже поклав у state саму різницю
        if (!doc["state"].is<JsonObject>()) {
            Serial.println("[SHADOW] Дельта без state — пропускаємо");
            return false;
        }
        delta = doc["state"];
    }

    // ── Витягуємо тільки те, що реально прийшло ──
    // Поля, яких у дельті немає, чіпати НЕ МОЖНА: їх відсутність
    // означає «збігається», а не «скинути в нуль».
    if (delta["indicator"].is<const char*>()) {
        const char* v = delta["indicator"];
        strncpy(out->indicator, v, sizeof(out->indicator) - 1);
        out->indicator[sizeof(out->indicator) - 1] = '\0';
        out->hasIndicator = true;

        Serial.print("[SHADOW] Індикація: ");
        Serial.println(out->indicator);
    }

    if (delta["interval"].is<int>()) {
        out->interval    = delta["interval"];
        out->hasInterval = true;

        Serial.print("[SHADOW] Бажаний interval: ");
        Serial.println(out->interval);
    }

    return (out->hasIndicator || out->hasInterval);
}

// ═══════════════════════════════════════════════════════════
// ЗВІТ. Без нього дельта прилітатиме ЩОРАЗУ.
//
// AWS рахує різницю як desired мінус reported. Поки ми не записали
// новий reported — з точки зору хмари ми бажане не виконали, і вона
// чесно нагадає про це на кожному пробудженні. Це не баг, це
// і є сенс механізму: стан вважається досягнутим тільки після
// підтвердження від пристрою.
// ═══════════════════════════════════════════════════════════
bool shadow_report(const char* indicator, int interval) {
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"state\":{\"reported\":{\"indicator\":\"%s\",\"interval\":%d}}}",
             indicator, interval);

    updateAccepted = false;
    updateRejected = false;

    mqtt_publish(TOPIC_SHADOW_UPDATE, payload);

    // ═══════════════════════════════════════════════════════════
    // ЧОМУ ТУТ ЧЕКАННЯ, А НЕ «опублікували і поїхали».
    //
    // Одразу після цього виклику ми рвемо TLS-зʼєднання і йдемо спати.
    // QoS 0 не має підтвердження на рівні MQTT: publish() лише віддає
    // байти в сокет. Якщо AWS не встиг їх забрати або відхилив документ —
    // reported лишиться старим, дельта прилетить знову на наступному
    // пробудженні, і збоку це виглядає рівно як «Shadow не працює».
    //
    // Тому крутимо poll, поки не приїде accepted або rejected. Це та сама
    // схема, що в mqtt_wait_for_job() — інакше відповідь просто нікому
    // прочитати з сокета.
    // ═══════════════════════════════════════════════════════════
    unsigned long start = millis();

    while (millis() - start < 2000) {
        mqtt_poll();

        if (updateAccepted) {
            Serial.print("[SHADOW] AWS прийняв за ");
            Serial.print(millis() - start);
            Serial.println(" мс — reported оновлено");
            return true;
        }

        if (updateRejected) {
            // Тіло відмови каже, що саме не так. 403 Forbidden —
            // у Policy немає дозволу на shadow/update.
            Serial.print("[SHADOW] AWS ВІДХИЛИВ: ");
            Serial.println(rejectBuf);
            return false;
        }

        delay(10);
    }

    // Ні accepted, ні rejected. Найчастіше означає, що підписку на ці
    // топіки не пропустила Policy — тоді відповідь фізично не доїде.
    Serial.println("[SHADOW] Підтвердження не прийшло — перевір Policy на shadow/update");
    return false;
}
