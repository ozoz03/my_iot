#include <Arduino.h>
#include "secrets.h"
#include "mqtt/mqtt.h"
#include "dht/dht.h"
#include "ldr/ldr.h"
#include "led/led.h"
#include "button/button.h"

// ═══════════════════════════════════════════════════════════
// ТАЙМІНГИ
// ═══════════════════════════════════════════════════════════
#define MONITOR_INTERVAL 5000   // мс — читання сенсорів
#define PUBLISH_INTERVAL 30000  // мс — публікація в AWS IoT Core

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
    Serial.println("ESP32 старт (AWS IoT Core, двостороння комунікація)");

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
    unsigned long now = millis();

    checkButton();

    // Читання сенсорів НЕ залежить від стану MQTT — локальний моніторинг
    // і так має працювати офлайн, публікація вниз тільки коли є звʼязок.
    if ((now - lastMonitor) >= MONITOR_INTERVAL) {
        lastMonitor = now;
        readSensors();
    }

    if (mqtt_connected()) {
        mqtt_poll();   // ОБОВ'ЯЗКОВО кожну ітерацію — тримає Keep Alive

        const char* cmd = mqtt_take_command();
        if (cmd) {
            led_handle_command(cmd);
        }

        if ((now - lastPublish) >= PUBLISH_INTERVAL) {
            lastPublish = now;
            publishData(lastGoodData);
        }
    } else {
        mqtt_reconnect_tick();
    }
}
