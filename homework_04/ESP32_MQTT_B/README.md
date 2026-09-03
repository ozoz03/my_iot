# Лекція 8 — MQTT Subscriber + Actuator (ESP32-B) — ДЗ4

Підписка ESP32 (QoS 1) на топік сенсорів і топік команд ESP32-A, керування LED актуатором на основі температури та на команду `"manual_read"` (неблокуюче блимання 3 рази). Callback функція автоматично спрацьовує при кожному вхідному повідомленні. Автоматичний reconnect через `millis()` відновлює з'єднання і підписки без `delay()`, з обмеженням у 3 послідовні спроби (Arduino Framework, PlatformIO + Wokwi).

---

## Структура проєкту

```
src/
└── main.cpp      — основний файл прошивки
diagram.json      — схема підключення для Wokwi-симулятора
wokwi.toml        — конфігурація Wokwi
platformio.ini    — конфігурація PlatformIO (платформа, плата, швидкість монітора)
```

---

## Залежності

```ini
lib_deps =
    knolleary/PubSubClient
```

`WiFi.h` входить до складу ESP32 Arduino core — додаткових бібліотек не потрібно.

---

## Конфігурація

```cpp
#define LED_PIN        2                            // GPIO2 — актуатор

#define WIFI_SSID      "Wokwi-GUEST"                // SSID мережі (Wokwi симулятор)
#define WIFI_PASSWORD  ""                            // пароль (порожній для Wokwi-GUEST)
#define WIFI_TIMEOUT   10000                         // таймаут підключення, мс

#define MQTT_BROKER    "broker.hivemq.com"           // публічний MQTT брокер
#define MQTT_PORT      1883                          // plain TCP, без TLS
#define MQTT_CLIENT_ID "esp32-ozoz03-b"              // унікальний — не як у ESP32-A!

#define TOPIC_SENSORS  "iot-course/ozoz03/sensors"    // підписка (QoS 1) — дані з ESP32-A
#define TOPIC_COMMANDS "iot-course/ozoz03/commands"   // підписка (QoS 1) — команди з ESP32-A
#define SUBSCRIBE_QOS  1

#define RECONNECT_INTERVAL          5000             // інтервал між спробами reconnect, мс
#define MQTT_MAX_RECONNECT_ATTEMPTS 3                // максимум спроб reconnect поспіль
#define BLINK_TOGGLE_COUNT          6                // 3 blink = 6 перемикань LED
#define BLINK_INTERVAL              200              // мс між перемиканнями
```

---

## Опис main.cpp

### 1. Підключення до Wi-Fi — `connectWifi()`

```cpp
bool connectWifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
    // канал 6 — пропускає сканування, економить ~4 секунди в Wokwi
    // повертає true при успіху, false при таймауті
}
```

- Викликається один раз у `setup()`.
- Канал 6 — оптимізація для Wokwi-GUEST, пропускає фазу сканування.

### 2. Підключення до MQTT брокера — `connectMQTT()`

```cpp
bool connectMQTT() {
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
        // підключено — одразу підписуємось на обидва топіки з QoS 1
        mqttClient.subscribe(TOPIC_SENSORS, SUBSCRIBE_QOS);
        mqttClient.subscribe(TOPIC_COMMANDS, SUBSCRIBE_QOS);
    }
}
```

Підписка викликається всередині `connectMQTT()`, а не в `setup()`. Причина: `Clean Session = true` скидає всі підписки при кожному відключенні. Так підписка автоматично відновлюється після кожного reconnect.

- `setKeepAlive(60)` — ESP32 надсилає PING брокеру кожні 60 секунд.
- `setSocketTimeout(30)` — таймаут TCP сокету 30 секунд.
- `setCallback(onMessage)` — реєструється до `connectMQTT()`, в `setup()`.

### 3. Callback функція — `onMessage(topic, payload, length)`

```cpp
void onMessage(char* topic, byte* payload, unsigned int length) {
    // byte* → char* (додаємо термінатор '\0')
    // якщо topic == TOPIC_COMMANDS і payload == "manual_read":
    //   Serial.println("Manual trigger received") + startBlink()
    // якщо topic == TOPIC_SENSORS:
    //   парсимо температуру через strstr + atof, керуємо LED
}
```

Правила callback:
- `payload` — масив байтів без термінатора. Завжди конвертувати через `memcpy` + `'\0'`.
- Не використовувати `delay()` всередині — блокує `loop()`.
- Не використовувати `mqttClient.publish()` всередині — може спричинити проблеми.
- Викликається автоматично всередині `mqttClient.loop()`.

### 4. Парсинг JSON — `strstr()` + `atof()`

```cpp
char* tempPtr = strstr(message, "\"temperature\":");
float temperature = atof(tempPtr + 14);
// 14 — довжина рядка "temperature": разом з лапками і двокрапкою
```

Простий парсинг без бібліотеки ArduinoJson — достатньо для payload з відомою структурою. Для складніших або змінних структур використовуйте ArduinoJson.

### 5. Логіка керування LED

```cpp
if (temperature > 26.0) {
    digitalWrite(LED_PIN, HIGH);  // вище 26°C — LED ON
} else if (temperature < 20.0) {
    digitalWrite(LED_PIN, LOW);   // нижче 20°C — LED OFF
}
// між 20°C і 26°C — без змін
// якщо в цей момент триває blink (manual_read) — температурна логіка пропускається,
// щоб не збивати послідовність блимання
```

### 6. Неблокуюче блимання на `"manual_read"` — `startBlink()` / `handleBlink()`

`onMessage()` лише виставляє прапорці (`blinkActive`, `blinkTogglesLeft`) — сам blink виконується в `loop()` через `handleBlink()`, що перемикає `LED_PIN` кожні `BLINK_INTERVAL` мс без `delay()`. Після `BLINK_TOGGLE_COUNT` (6) перемикань — це 3 повних ON/OFF — LED гарантовано вимикається.

### 7. Неблокуючий reconnect з лімітом спроб

```cpp
void loop() {
    handleBlink();

    if (mqttClient.connected()) {
        mqttClient.loop();  // підтримує Keep Alive і викликає callback
        mqttReconnectAttempts = 0;
        mqttReconnectExhausted = false;
    } else if (!mqttReconnectExhausted) {
        if ((now - lastReconnectAttempt) > RECONNECT_INTERVAL) {
            mqttReconnectAttempts++;
            if (!connectMQTT() && mqttReconnectAttempts >= MQTT_MAX_RECONNECT_ATTEMPTS) {
                mqttReconnectExhausted = true;  // більше не намагаємось reconnect
            }
        }
    }
}
```

`mqttClient.loop()` викликається тільки коли підключено — без нього callback не спрацьовує і брокер не отримує PING. Після 3 невдалих спроб поспіль пристрій припиняє намагатись reconnect.

---

## Вивід у Serial

```
ESP32-B старт
[Wi-Fi] Підключаємось.... OK
[Wi-Fi] IP: 10.13.37.2
[MQTT] Підключаємось до broker.hivemq.com... OK
[MQTT] Підписались на: iot-course/ozoz03/sensors та iot-course/ozoz03/commands
[MQTT] Топік: iot-course/ozoz03/sensors
[MQTT] Payload: {"temperature":27.0,"humidity":55.0}
[MQTT] Температура: 27.00
[LED] ON — вище 26°C
[MQTT] Топік: iot-course/ozoz03/commands
[MQTT] Payload: manual_read
Manual trigger received
```

При втраті з'єднання (максимум 3 спроби):

```
[MQTT] З'єднання втрачено — спроба 1/3
[MQTT] Підключаємось до broker.hivemq.com... помилка: -2
[MQTT] З'єднання втрачено — спроба 2/3
[MQTT] Підключаємось до broker.hivemq.com... помилка: -2
[MQTT] З'єднання втрачено — спроба 3/3
[MQTT] Підключаємось до broker.hivemq.com... помилка: -2
[MQTT] Досягнуто максимум спроб reconnect — припиняємо, поки Wi-Fi/брокер не відновляться
```

---

## Як запустити

1. Переконайтесь що ESP32-A вже запущений і публікує дані.
2. Відкрити проєкт у VS Code з розширенням **PlatformIO**.
3. Для симуляції — встановити розширення **Wokwi for VS Code** і натиснути `F1 → Wokwi: Start Simulator`.
4. Відкрити **Serial Monitor** (швидкість: `115200`).
5. Після підключення до брокера ESP32-B автоматично отримує повідомлення від ESP32-A і керує LED.

---

## Рекомендована література

### Документація

| Ресурс | Посилання |
|---|---|
| PubSubClient — офіційна документація | [pubsubclient.knolleary.net](https://pubsubclient.knolleary.net) |
| HiveMQ публічний брокер | [hivemq.com/mqtt/public-mqtt-broker](https://www.hivemq.com/mqtt/public-mqtt-broker) |
| MQTT Essentials серія | [hivemq.com/blog/mqtt-essentials-part-1-introducing-mqtt](https://www.hivemq.com/blog/mqtt-essentials-part-1-introducing-mqtt/) |
| Wokwi — ESP32 Wi-Fi у симуляторі | [docs.wokwi.com/guides/esp32-wifi](https://docs.wokwi.com/guides/esp32-wifi) |
| Random Nerd Tutorials — ESP32 MQTT | [randomnerdtutorials.com/esp32-mqtt-publish-subscribe-arduino-ide](https://randomnerdtutorials.com/esp32-mqtt-publish-subscribe-arduino-ide/) |

### Відео

- **MQTT Essentials відео серія** — [youtube.com/playlist?list=PLRkdoPznE1EMXLW6XoYLGd4uUaB6wB0wd](https://www.youtube.com/playlist?list=PLRkdoPznE1EMXLW6XoYLGd4uUaB6wB0wd)
- **MQTT Explorer туторіал** — [youtube.com/watch?v=DApS6cMCsrA](https://www.youtube.com/watch?v=DApS6cMCsrA)

### Книги

- **"Internet of Things with ESP32"** — Agus Kurniawan
  Практичні приклади Wi-Fi, HTTP, MQTT на ESP32.

- **"Making Embedded Systems"** — Elecia White
  Мережева взаємодія, протоколи та надійність з'єднань.