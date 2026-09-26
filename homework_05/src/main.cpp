#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "DHT.h"
#include "secrets.h"

// ═══════════════════════════════════════════════════════════
// ПІНИ
// ═══════════════════════════════════════════════════════════
#define LED_PIN    2
#define BUTTON_PIN 5
#define LDR_PIN    34
#define DHTT_PIN   4
#define DHTT_TYPE  DHT22

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ WI-FI
// ═══════════════════════════════════════════════════════════
#define WIFI_TIMEOUT  10000           // максимум 10 секунд на підключення

// ═══════════════════════════════════════════════════════════
// КОНФІГУРАЦІЯ MQTT (AWS IoT Core)
// ═══════════════════════════════════════════════════════════
#define MQTT_PORT       8883                                  // MQTT over TLS, НЕ 1883!
#define TOPIC_TELEMETRY "iot-course/ozasymenko/sensors/data"  // топік для публікації

// Таймер reconnect — чекаємо 5 секунд між спробами
unsigned long lastReconnectAttempt = 0;
#define RECONNECT_INTERVAL 5000  // мс

// Чи вдалась остання синхронізація часу — якщо ні, TLS-handshake завжди
// падатиме (state -2), тому перед кожною спробою MQTT-перепідключення
// синхронізацію часу треба повторити, а не лише сам connectMQTT()
bool timeOk = false;

// ═══════════════════════════════════════════════════════════
// ТАЙМІНГИ
// ═══════════════════════════════════════════════════════════
#define DEBOUNCE_DELAY   50     // мс
#define MONITOR_INTERVAL 5000   // мс — читання сенсорів у режимі "моніторинг"
#define PUBLISH_INTERVAL 30000  // мс — публікація в AWS IoT Core

// Якщо останнє вдале вимірювання DHT22 старіше за це — дані вважаються
// застарілими (сенсор мовчить), publishData() їх не публікує, щоб не
// видавати старі temperature/humidity за свіжий вимір з новим timestamp
#define STALE_THRESHOLD (PUBLISH_INTERVAL * 3)

// Поріг освітленості (люкси), нижче якого вмикається LED.
// Значення підібране під дефолтну яскравість LDR-слайдера у Wokwi;
// при тестуванні перетягніть повзунок фоторезистора, щоб побачити обидва стани.
#define LUX_THRESHOLD 100.0f

// ═══════════════════════════════════════════════════════════
// РЕЖИМИ РОБОТИ
// ═══════════════════════════════════════════════════════════
enum Mode { MODE_SILENT, MODE_MONITORING };
Mode currentMode = MODE_SILENT;

// ── Debounce кнопки ──────────────────────────────────────
bool lastRawState = HIGH;
bool stableState  = HIGH;
unsigned long lastDebounceTime = 0;

// ── Неблокуючі таймери ───────────────────────────────────
unsigned long lastMonitor = 0;
unsigned long lastPublish = 0;

// ── Дані сенсорів (зберігаються між циклами публікації) ──
struct SensorData {
  float temperature;
  float humidity;
  float lux;
  bool  hasData;             // true лише після першого реального вимірювання
  unsigned long lastGoodMillis;  // millis() останнього вдалого читання DHT22
};

SensorData lastGoodData = { 0.0f, 0.0f, 0.0f, false, 0 };

DHT dht(DHTT_PIN, DHTT_TYPE);

// ═══════════════════════════════════════════════════════════
// MQTT + TLS КЛІЄНТ
// ═══════════════════════════════════════════════════════════
// WiFiClientSecure замість WiFiClient — єдина зміна на цьому рівні (слайд 17)
WiFiClientSecure net;
PubSubClient      mqttClient(net);

// ═══════════════════════════════════════════════════════════
// КОНВЕРТАЦІЯ ADC → LUX
// ═══════════════════════════════════════════════════════════
float adcToLux(int adcValue) {
  const float GAMMA = 0.7f;
  const float RL10  = 33.0f;  // опір LDR при 10 lux (кОм)

  float voltage    = adcValue / 4096.0f * 3.3f;
  float resistance = 2000.0f * voltage / (1.0f - voltage / 3.3f);
  return pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1.0f / GAMMA));
}

// ═══════════════════════════════════════════════════════════
// КНОПКА З DEBOUNCE — перемикання "тиша" / "моніторинг"
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
        currentMode = (currentMode == MODE_SILENT) ? MODE_MONITORING : MODE_SILENT;
        Serial.print("[Режим] ");
        Serial.println(currentMode == MODE_MONITORING ? "МОНІТОРИНГ" : "ТИША");

        if (currentMode == MODE_SILENT) {
          digitalWrite(LED_PIN, LOW);  // у тиші LED не повинен лишатись увімкненим
        }
      }
    }
  }

  lastRawState = raw;
}

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

    // DHCP у Wokwi іноді не віддає робочий DNS-сервер — тоді hostByName()
    // падає з "DNS Failed" ще до TLS, навіть коли Wi-Fi і IP вже є.
    // Прописуємо публічний DNS явно, лишаючи вже отримані IP/gateway/subnet.
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));

    return true;
}

// ═══════════════════════════════════════════════════════════
// NTP — синхронізація часу
// БЕЗ ЦЬОГО TLS ВПАДЕ, навіть з правильними сертифікатами:
// ESP32 стартує з 1970 року, і handshake вважає сертифікат AWS
// "ще не дійсним", бо 1970 < дата видачі сертифіката
// ═══════════════════════════════════════════════════════════
#define NTP_TIMEOUT 15000  // мс

bool syncTime() {
    Serial.print("[NTP] Синхронізація часу");
    configTime(0, 0, "pool.ntp.org");  // зсув 0, DST 0 — для TLS достатньо

    struct tm timeinfo;
    unsigned long start = millis();
    while (!getLocalTime(&timeinfo)) {
        if (millis() - start > NTP_TIMEOUT) {
            Serial.println(" таймаут!");
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println(" OK");
    return true;
}

// ═══════════════════════════════════════════════════════════
// ПІДКЛЮЧЕННЯ ДО AWS IOT CORE
// Порядок кроків критичний: Wi-Fi → час → сертифікати → сервер
// ═══════════════════════════════════════════════════════════
void connectAWS() {
    // 1. Wi-Fi
    connectWifi();

    // 2. синхронізуємо час — до сертифікатів, до connect()
    timeOk = syncTime();

    // 3. заряджаємо три файли з secrets.h у TLS-клієнт
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

    // Буфер PubSubClient за замовчуванням 256 байт — замалий,
    // мовчки обрізає JSON. Збільшуємо до підключення.
    mqttClient.setBufferSize(512);
}

// ═══════════════════════════════════════════════════════════
// MQTT CONNECT
// Client ID = THINGNAME — Policy обмежує Connect саме по ньому
// ═══════════════════════════════════════════════════════════
bool connectMQTT() {
    Serial.print("[MQTT] Підключаємось до AWS IoT Core...");

    if (mqttClient.connect(THINGNAME)) {
        Serial.println(" OK");
        return true;
    }

    // mqttClient.state() коди: -2 = TLS/handshake помилка
    // (перевір час і endpoint), 5 = відмовлено (перевір Policy)
    Serial.print(" помилка: ");
    Serial.println(mqttClient.state());
    return false;
}

// ═══════════════════════════════════════════════════════════
// ЗЧИТУВАННЯ СЕНСОРІВ — завжди, незалежно від режиму
// (публікація в хмару не повинна залежати від режиму відображення).
// Режим лише вмикає/вимикає Serial-вивід і поведінку LED нижче.
// ═══════════════════════════════════════════════════════════
void readSensors() {
  int   rawLdr = analogRead(LDR_PIN);
  float lux    = adcToLux(rawLdr);

  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  bool  dhtOk        = !isnan(temperature) && !isnan(humidity);

  if (!dhtOk && currentMode == MODE_MONITORING) {
    Serial.println("[Помилка] DHT22: сенсор повернув некоректні дані (NaN) — пропускаємо, продовжуємо роботу");
  }
  if (dhtOk) {
    lastGoodData.temperature   = temperature;
    lastGoodData.humidity      = humidity;
    lastGoodData.lastGoodMillis = millis();
  }
  lastGoodData.lux = lux;
  lastGoodData.hasData = true;

  if (currentMode != MODE_MONITORING) {
    return;
  }

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
  Serial.print(lux, 1);
  Serial.print(" (ADC ");
  Serial.print(rawLdr);
  Serial.println(")");

  bool ledOn = lux < LUX_THRESHOLD;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  Serial.println(ledOn ? "[LED] Увімкнено (низька освітленість)" : "[LED] Вимкнено");
}

// ═══════════════════════════════════════════════════════════
// ПУБЛІКАЦІЯ ДАНИХ У AWS IOT CORE
// ═══════════════════════════════════════════════════════════
void publishData(const SensorData &data) {
    if (!data.hasData) {
        Serial.println("[MQTT] Ще немає жодного вимірювання — пропускаємо публікацію");
        return;
    }

    if (millis() - data.lastGoodMillis > STALE_THRESHOLD) {
        Serial.println("[MQTT] Дані застарілі (DHT22 мовчить) — пропускаємо публікацію");
        return;
    }

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
        THINGNAME, (unsigned long)now, data.temperature, data.humidity, data.lux);

    Serial.print("[MQTT] Публікуємо: ");
    Serial.println(payload);

    bool ok = mqttClient.publish(TOPIC_TELEMETRY, payload);
    Serial.println(ok ? "[MQTT] OK" : "[MQTT] Помилка публікації");
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32 старт (AWS IoT Core edition). Поточний режим: ТИША");

    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LDR_PIN, INPUT);

    dht.begin();

    connectAWS();

    mqttClient.setKeepAlive(60);        // PING кожні 60 секунд
    mqttClient.setSocketTimeout(30);    // таймаут TCP сокету 30 секунд

    connectMQTT();
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    unsigned long now = millis();

    handleButton();

    if ((now - lastMonitor) >= MONITOR_INTERVAL) {
        lastMonitor = now;
        readSensors();
    }

    if (mqttClient.connected()) {
        mqttClient.loop();  // ОБОВ'ЯЗКОВО — підтримує Keep Alive

        if ((now - lastPublish) >= PUBLISH_INTERVAL) {
            lastPublish = now;
            publishData(lastGoodData);
        }
    } else {
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            Serial.println("[MQTT] З'єднання втрачено — перепідключаємось...");

            // Wi-Fi міг відвалитись незалежно від MQTT (на відміну від часу,
            // тут не потрібен окремий прапорець — WiFi.status() завжди дає
            // актуальний стан, кешований bool міг би так само застаріти,
            // як застарів timeOk без цього фікса)
            if (WiFi.status() != WL_CONNECTED) {
                connectWifi();
            }

            // Явно закриваємо стару TLS-сесію перед новою спробою —
            // інакше mbedTLS-контекст може лишитись "напівживим"
            net.stop();

            // Час міг не синхронізуватись при старті (UDP/123 не пройшов) —
            // без цього TLS-handshake завжди падатиме з state -2,
            // а сам connectMQTT() час повторно не синхронізує
            if (!timeOk) {
                timeOk = syncTime();
            }

            connectMQTT();
        }
    }
}
