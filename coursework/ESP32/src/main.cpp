#include <Arduino.h>
#include <esp_ota_ops.h>
#include "secrets.h"
#include "version.h"
#include "mqtt/mqtt.h"
#include "dht/dht.h"
#include "ldr/ldr.h"
#include "led/led.h"
#include "button/button.h"
#include "ota/ota.h"

// ═══════════════════════════════════════════════════════════
// ТАЙМІНГИ
// ═══════════════════════════════════════════════════════════
#define MONITOR_INTERVAL   5000    // мс — читання сенсорів
#define PUBLISH_INTERVAL   30000   // мс — публікація в AWS IoT Core
#define CONFIRM_TIMEOUT_MS 120000  // скільки нова прошивка має, щоб дійти до MQTT

// Якщо останнє вдале вимірювання DHT22 старіше за це — дані вважаються
// застарілими (сенсор мовчить), publishData() їх не публікує, щоб не
// видавати старі temperature/humidity за свіжий вимір з новим timestamp
#define STALE_THRESHOLD (PUBLISH_INTERVAL * 3)

// ── Дані сенсорів (зберігаються між циклами публікації) ──
struct SensorData {
  float temperature;
  float humidity;
  float lux;
  bool  hasData;                  // true лише після першого реального вимірювання
  unsigned long lastGoodMillis;   // millis() останнього вдалого читання DHT22
};

static SensorData lastGoodData = { 0.0f, 0.0f, 0.0f, false, 0 };

// ── Неблокуючі таймери ───────────────────────────────────
static unsigned long lastMonitor = 0;
static unsigned long lastPublish = 0;

// true — ця прошивка щойно прийшла через OTA і ще на випробувальному терміні
static bool imagePending = false;

// ═══════════════════════════════════════════════════════════
// ПІДТВЕРДЖЕННЯ ОБРАЗУ: «я не просто завантажився — я працюю»
//
// Записати прошивку у флеш — ще не означає оновитись. Ядро зібране з
// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y, а це вмикає в завантажувачі
// випробувальний термін для кожного нового образу:
//
//   Update.end(true)      → слот стає NEW
//   завантажувач          → бачить NEW, ставить PENDING_VERIFY і запускає
//   підтвердження         → VALID, образ прийнято назавжди
//   ресет до підтвердження → ABORTED, слот викреслено, вантажимо старий
//
// ПАСТКА: Arduino за замовчуванням підтверджує образ САМ — ще до setup(),
// у initArduino(). «Дожив до setup()» = «прийнято назавжди». Rollback тоді
// рятує лише від прошивки, що падає на старті. Прошивка, яка стартує, але
// не може підключитись, буде підтверджена — і пристрій більше ніколи не
// отримає наступне OTA (воно приходить через MQTT).
//
// Тому забираємо рішення собі: verifyRollbackLater() повертає true —
// «не підтверджуй, я вирішу сам». Доказ життя тут — підключення до MQTT.
//
// extern "C" обовʼязковий: ядро оголошує цю функцію в C-файлі як «слабку»
// (weak). Без extern "C" компілятор C++ дасть їй інше імʼя, ядро її не
// побачить і мовчки підтвердить образ саме — жодної помилки не буде.
// ═══════════════════════════════════════════════════════════
extern "C" bool verifyRollbackLater() {
    return true;
}

// Викликається і з loop(), і як idle-callback з net.cpp під час
// Wi-Fi/NTP-очікування — інакше кнопка "не працює", поки йде підключення.
static void checkButton() {
    if (button_pressed()) {
        Serial.println("[Кнопка] Натиснуто");
    }
}

// ═══════════════════════════════════════════════════════════
// ЗЧИТУВАННЯ СЕНСОРІВ — завжди, кожні MONITOR_INTERVAL
// ═══════════════════════════════════════════════════════════
static void readSensors() {
  float lux = ldr_read_lux();

  float temperature, humidity;
  bool  dhtOk = dht_read(temperature, humidity);

  if (!dhtOk) {
    Serial.println("[Помилка] DHT22: сенсор повернув некоректні дані (NaN) — пропускаємо, продовжуємо роботу");
  } else {
    lastGoodData.temperature    = temperature;
    lastGoodData.humidity       = humidity;
    lastGoodData.lastGoodMillis = millis();
  }
  lastGoodData.lux = lux;
  lastGoodData.hasData = true;

  char tempStr[8];
  char humStr[8];
  if (dhtOk) {
    snprintf(tempStr, sizeof(tempStr), "%.1f", temperature);
    snprintf(humStr, sizeof(humStr), "%.1f", humidity);
  } else {
    strcpy(tempStr, "err");
    strcpy(humStr, "err");
  }

  Serial.print("[Сенсори] t=");
  Serial.print(tempStr);
  Serial.print("C  h=");
  Serial.print(humStr);
  Serial.print("%  lux=");
  Serial.println(lux, 1);
}

// ═══════════════════════════════════════════════════════════
// ПУБЛІКАЦІЯ ДАНИХ У AWS IOT CORE
// ═══════════════════════════════════════════════════════════
static void publishData(const SensorData &data) {
    if (!data.hasData) {
        Serial.println("[MQTT] Ще немає жодного вимірювання — пропускаємо публікацію");
        return;
    }

    if (millis() - data.lastGoodMillis > STALE_THRESHOLD) {
        Serial.println("[MQTT] Дані застарілі (DHT22 мовчить) — пропускаємо публікацію");
        return;
    }

    mqtt_publish_telemetry(THINGNAME, data.temperature, data.humidity, data.lux);
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);

    // Три числа, які розрізняють усі сценарії OTA за одну секунду:
    //   розділ app1 + причина 8 (DEEPSLEEP) → оновились, усе добре
    //   розділ app0 після оновлення        → відкотились
    //   причина 5 (INT_WDT) / 6 (TASK_WDT) → watchdog під час запису
    // Коди: 1 POWERON, 3 SW, 4 PANIC, 5 INT_WDT, 6 TASK_WDT, 8 DEEPSLEEP
    Serial.printf("ESP32 — OTA, версія %s, розділ %s, причина ресету %d\n",
                  FIRMWARE_VERSION,
                  esp_ota_get_running_partition()->label,
                  (int)esp_reset_reason());

    // Чи ми на випробувальному терміні? Після звичайної прошивки через
    // кабель чи Wokwi — ні: такий образ підтверджувати не треба.
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK
        && state == ESP_OTA_IMG_PENDING_VERIFY) {
        imagePending = true;
        Serial.println("[OTA] Нова прошивка на випробуванні — чекаю MQTT");
    }

    led_begin();
    button_begin();
    dht_begin();
    ldr_begin();

    if (mqtt_begin()) {
        mqtt_connect();   // перший виклик; далі — з loop()
    }
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    // Поки йде оновлення — не робимо нічого іншого. При синхронному
    // ota_handle_job() сюди під час завантаження не заходимо; прапорець
    // потрібен на випадок винесення завантаження в окрему задачу.
    if (otaInProgress) return;

    unsigned long now = millis();

    checkButton();

    // Читання сенсорів НЕ залежить від стану MQTT — локальний моніторинг
    // і так має працювати офлайн, публікація вниз тільки коли є звʼязок.
    if ((now - lastMonitor) >= MONITOR_INTERVAL) {
        lastMonitor = now;
        readSensors();
    }

    if (!mqtt_connected()) {
        // Нова прошивка не дійшла до MQTT за відведений час — вона не
        // робоча. Не чекаємо випадкового ресету: відкочуємось самі.
        if (imagePending && millis() > CONFIRM_TIMEOUT_MS) {
            Serial.println("[OTA] Нова прошивка не підключилась — відкат на попередню");
            Serial.flush();
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        mqtt_reconnect_tick();
        return;
    }

    // MQTT є — отже, наступне OTA до нас долетить. Тепер можна підтверджувати.
    if (imagePending) {
        esp_ota_mark_app_valid_cancel_rollback();
        imagePending = false;
        Serial.println("[OTA] Образ підтверджено — оновлення прийнято");
    }

    mqtt_poll();   // ОБОВ'ЯЗКОВО кожну ітерацію — тримає Keep Alive

    const char* cmd = mqtt_take_command();
    if (cmd) {
        led_handle_command(cmd);
    }

    // ── Робота / OTA ──
    // Виявили роботу — віддаємо в ota. Далі два результати:
    //   успіх   → рестарт усередині, сюди не повернемось
    //   невдача → повернемось, otaInProgress уже знято
    const char* job = mqtt_take_job();
    if (job) {
        ota_handle_job(job);
        return;   // цей цикл віддали під роботу — телеметрію пропускаємо
    }

    if ((now - lastPublish) >= PUBLISH_INTERVAL) {
        lastPublish = now;
        publishData(lastGoodData);
    }
}
