#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "DHT.h"

// ═══════════════════════════════════════════════════════════
// ПІНИ
// ═══════════════════════════════════════════════════════════
#define LED_PIN    2
#define BUTTON_PIN 5
#define LDR_PIN    34
#define DHTT_PIN   4
#define DHTT_TYPE  DHT22

// ═══════════════════════════════════════════════════════════
// WI-FI ТА СЕРВЕР
// ═══════════════════════════════════════════════════════════
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_TIMEOUT  10000  // мс — таймаут першого підключення

#define SERVER_URL "http://httpbin.org/post"

// ═══════════════════════════════════════════════════════════
// ТАЙМІНГИ
// ═══════════════════════════════════════════════════════════
#define DEBOUNCE_DELAY   50     // мс
#define MONITOR_INTERVAL 5000   // мс — читання сенсорів у режимі "моніторинг"
#define SEND_INTERVAL    30000  // мс — відправка на сервер

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
unsigned long lastSend    = 0;

// ── Дані сенсорів (зберігаються між циклами відправки) ───
struct SensorData {
  float temperature;
  float humidity;
  float lux;
};

SensorData lastGoodData = { 0.0f, 0.0f, 0.0f };

DHT dht(DHTT_PIN, DHTT_TYPE);

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
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT) {
      Serial.println(" — таймаут, працюємо офлайн");
      return false;
    }
    delay(250);
    Serial.print(".");
  }

  Serial.println(" OK");
  Serial.print("[Wi-Fi] IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

// ═══════════════════════════════════════════════════════════
// ЗЧИТУВАННЯ СЕНСОРІВ + КЕРУВАННЯ LED (лише в режимі "моніторинг")
// ═══════════════════════════════════════════════════════════
void readAndPrintSensors() {
  int   rawLdr = analogRead(LDR_PIN);
  float lux    = adcToLux(rawLdr);

  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  bool  dhtOk        = !isnan(temperature) && !isnan(humidity);

  if (!dhtOk) {
    Serial.println("[Помилка] DHT22: сенсор повернув некоректні дані (NaN) — пропускаємо, продовжуємо роботу");
  } else {
    lastGoodData.temperature = temperature;
    lastGoodData.humidity    = humidity;
  }
  lastGoodData.lux = lux;

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
// ВІДПРАВКА ДАНИХ НА СЕРВЕР
// ═══════════════════════════════════════════════════════════
void sendData(const SensorData &data) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Wi-Fi недоступний — пропускаємо відправку, продовжуємо роботу");
    WiFi.reconnect();  // неблокуюча спроба відновити з'єднання у фоні
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  char payload[96];
  snprintf(payload, sizeof(payload),
           "{\"temperature\":%.1f,\"humidity\":%.1f,\"lux\":%.1f}",
           data.temperature, data.humidity, data.lux);

  Serial.print("[HTTP] Відправляємо: ");
  Serial.println(payload);

  int httpCode = http.POST((uint8_t *)payload, strlen(payload));

  if (httpCode > 0) {
    Serial.print("[HTTP] Сервер відповів кодом: ");
    Serial.println(httpCode);
  } else {
    Serial.print("[HTTP] Помилка запиту: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LDR_PIN, INPUT);

  dht.begin();

  Serial.println("ESP32 старт. Поточний режим: ТИША");
  connectWifi();  // одноразова спроба з таймаутом; при невдачі працюємо офлайн
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  handleButton();

  if (currentMode == MODE_MONITORING && (now - lastMonitor) >= MONITOR_INTERVAL) {
    lastMonitor = now;
    readAndPrintSensors();
  }

  if ((now - lastSend) >= SEND_INTERVAL) {
    lastSend = now;
    sendData(lastGoodData);
  }
}
