# Лекція 8 — MQTT Publisher (ESP32-A) — ДЗ4

Публікація реальних показників DHT22 (температура, вологість) у MQTT-топік кожні 10 секунд, і публікація команди `"manual_read"` при натисканні кнопки. Неблокуючий таймер на `millis()`. Автоматичний reconnect через `millis()` відновлює з'єднання без `delay()`: циклами по 3 спроби з паузою 60 с між циклами, і окремо обробляє відсутність Wi-Fi (Arduino Framework, PlatformIO + Wokwi).

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
    adafruit/DHT sensor library
```

`WiFi.h` входить до складу ESP32 Arduino core — додаткових бібліотек не потрібно.

---

## Конфігурація

```cpp
#define BUTTON_PIN 5                               // кнопка (INPUT_PULLUP)
#define DHTT_PIN   4                               // DHT22 data pin
#define DHTT_TYPE  DHT22

#define WIFI_SSID      "Wokwi-GUEST"               // SSID мережі (Wokwi симулятор)
#define WIFI_PASSWORD  ""                           // пароль (порожній для Wokwi-GUEST)
#define WIFI_TIMEOUT   10000                        // таймаут підключення, мс

#define MQTT_BROKER    "broker.hivemq.com"          // публічний MQTT брокер
#define MQTT_PORT      1883                         // plain TCP, без TLS
#define MQTT_CLIENT_ID "esp32-ozoz03-a"             // унікальний Client ID

#define TOPIC_SENSOR_TEMP "iot-course/ozoz03/sensors/temperature"  // публікація температури
#define TOPIC_SENSOR_HUM  "iot-course/ozoz03/sensors/humidity"     // публікація вологості
#define TOPIC_COMMANDS    "iot-course/ozoz03/commands"             // публікація "manual_read"
#define TOPIC_STATUS      "iot-course/ozoz03/status/esp32-a"       // online/offline (retained, LWT)

#define PUBLISH_INTERVAL           10000            // інтервал публікації, мс
#define RECONNECT_INTERVAL         5000             // інтервал між спробами reconnect, мс
#define MQTT_MAX_RECONNECT_ATTEMPTS 3               // максимум спроб reconnect поспіль
#define RECONNECT_COOLDOWN         60000            // пауза перед новим циклом спроб, мс
```

---

## Опис main.cpp

### 1. Підключення до Wi-Fi — `connectWifi()`

```cpp
bool connectWifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);
    // канал 6 — пропускає сканування, економить ~4 секунди в Wokwi
    // чекає підключення з таймаутом WIFI_TIMEOUT
    // повертає true при успіху, false при таймауті
}
```

- Викликається один раз у `setup()`.
- Канал 6 (`WiFi.begin(..., 6)`) — оптимізація для Wokwi-GUEST, пропускає фазу сканування.
- Якщо за `WIFI_TIMEOUT` мс з'єднання не встановлено — повертає `false`.

### 2. Підключення до MQTT брокера — `connectMQTT()`

```cpp
bool connectMQTT() {
    // LWT: якщо з'єднання обірветься нештатно, брокер сам опублікує
    // "offline" (retained) у TOPIC_STATUS
    if (mqttClient.connect(MQTT_CLIENT_ID, TOPIC_STATUS, 1, true, "offline")) {
        mqttClient.publish(TOPIC_STATUS, "online", true);  // retained
    }
    // mqttClient.state() — код помилки при невдачі:
    // -4 = таймаут, -2 = сервер не знайдено, 5 = відмовлено в доступі
}
```

- MQTT Client ID має бути унікальним на брокері.
- Якщо два клієнти мають однаковий Client ID — перший буде відключений.
- `setKeepAlive(60)` — ESP32 надсилає PING брокеру кожні 60 секунд.
- `setSocketTimeout(30)` — таймаут TCP сокету 30 секунд.
- LWT (`willTopic`/`willMessage`) — брокер сам публікує `"offline"` у `TOPIC_STATUS`, якщо TCP-з'єднання обірветься без штатного `DISCONNECT` (втрата Wi-Fi, живлення тощо). Так підписники бачать, що пристрій відпав, навіть не опитуючи його напряму.

### 3. Публікація даних сенсорів — `publishSensors()`

```cpp
void publishSensors() {
    // читає dht.readTemperature() / dht.readHumidity()
    // якщо NaN (сенсор не готовий/помилка) — пропускає публікацію
    // кожен показник — окремим повідомленням у свій підтопік
    // виконує mqttClient.publish() і виводить результат у Serial
}
```

Payload — просто число, без JSON:

```
TOPIC_SENSOR_TEMP → "24.5"
TOPIC_SENSOR_HUM  → "55.0"
```

Окремі підтопіки на кожен показник дозволяють підписатись лише на потрібне значення (наприклад ESP32-B слухає тільки температуру) без розбору JSON. `snprintf()` замість Arduino `String` — уникаємо фрагментації heap (Заняття 4).

### 4. Ручний запит по кнопці — `publishManualRead()` + `handleButton()`

Кнопка (GPIO5, `INPUT_PULLUP`) опитується з debounce (50 мс), як у ДЗ3. На стабільний перехід у `LOW` публікується рядок `"manual_read"` (не JSON) у `TOPIC_COMMANDS`.

### 5. Неблокуючий таймер і reconnect з циклами спроб — `maintainConnection()`

```cpp
void loop() {
    handleButton();

    if (mqttClient.connected()) {
        mqttClient.loop();  // підтримує Keep Alive і обробляє вхідні повідомлення
        mqttReconnectAttempts = 0;
        mqttReconnectExhausted = false;

        if ((now - lastPublish) > PUBLISH_INTERVAL) {
            publishSensors();
        }
    } else {
        maintainConnection();
    }
}

void maintainConnection() {
    // цикл вичерпано минулого разу — чекаємо RECONNECT_COOLDOWN, тоді скидаємо
    // лічильник і стартуємо новий цикл спроб (а не зависаємо офлайн назавжди)
    if (mqttReconnectExhausted) {
        if (now - reconnectCooldownStart < RECONNECT_COOLDOWN) return;
        mqttReconnectAttempts  = 0;
        mqttReconnectExhausted = false;
    }

    if ((now - lastReconnectAttempt) <= RECONNECT_INTERVAL) return;
    lastReconnectAttempt = now;

    // без Wi-Fi спроба MQTT завідомо провалиться — не палимо на це лічильник
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();  // неблокуючий поштовх
        return;
    }

    mqttReconnectAttempts++;
    if (!connectMQTT() && mqttReconnectAttempts >= MQTT_MAX_RECONNECT_ATTEMPTS) {
        mqttReconnectExhausted = true;
        reconnectCooldownStart = now;
    }
}
```

`mqttClient.loop()` викликається тільки коли підключено — без нього брокер не отримує PING і відключає клієнта.

`maintainConnection()` розділяє дві причини відсутності з'єднання:
- **Немає Wi-Fi** — спроба MQTT тут завідомо марна, тож лічильник спроб не витрачається: замість цього неблокуюче `WiFi.reconnect()`.
- **Wi-Fi є, брокер не відповідає** — це і є "справжня" спроба reconnect, вона враховується в лічильнику.

Після `MQTT_MAX_RECONNECT_ATTEMPTS` невдалих спроб поспіль пристрій не зависає офлайн назавжди: чекає паузу `RECONNECT_COOLDOWN` (60 с), скидає лічильник і починає новий цикл спроб. Той самий лічильник і прапорець `mqttReconnectExhausted` також скидаються одразу після успішного підключення. У `setup()` результат `connectWifi()` перевіряється — якщо Wi-Fi одразу не піднявся, `connectMQTT()` там навіть не викликається (сенсу немає), і `maintainConnection()` бере це на себе з першої ж ітерації `loop()`.

---

## Вивід у Serial

```
ESP32-A старт
[Wi-Fi] Підключаємось.... OK
[Wi-Fi] IP: 10.13.37.2
[MQTT] Підключаємось до broker.hivemq.com... OK
[MQTT] Публікуємо temperature=27.0 humidity=55.0
[MQTT] OK
[MQTT] Публікуємо команду: manual_read
[MQTT] OK
```

При втраті з'єднання (Wi-Fi є, брокер не відповідає — максимум 3 спроби, потім пауза 60 с і новий цикл):

```
[MQTT] З'єднання втрачено — спроба 1/3
[MQTT] Підключаємось до broker.hivemq.com... помилка: -2
[MQTT] З'єднання втрачено — спроба 2/3
[MQTT] Підключаємось до broker.hivemq.com... помилка: -2
[MQTT] З'єднання втрачено — спроба 3/3
[MQTT] Підключаємось до broker.hivemq.com... помилка: -2
[MQTT] Досягнуто максимум спроб reconnect — пауза 60с перед новим циклом
... (60 секунд тиші) ...
[MQTT] Пауза закінчилась — починаємо новий цикл спроб reconnect
[MQTT] З'єднання втрачено — спроба 1/3
```

Якщо ж пропав саме Wi-Fi — лічильник спроб не витрачається, натомість неблокуючий `WiFi.reconnect()` раз на 5 с:

```
[Wi-Fi] З'єднання втрачено — перепідключаємось...
[Wi-Fi] З'єднання втрачено — перепідключаємось...
```

---

## Моніторинг через HiveMQ веб-клієнт

1. Відкрити `hivemq.com/demos/websocket-client`
2. Host: `broker.hivemq.com`, Port: `8884`
3. Натиснути **Connect** (зелена крапка = підключено)
4. **Add New Topic Subscription** → топік: `iot-course/ozoz03/#`
5. Запустити симуляцію в Wokwi — повідомлення з'являться в секції **Messages**

---

## Як запустити

1. Відкрити проєкт у VS Code з розширенням **PlatformIO**.
2. Для симуляції — встановити розширення **Wokwi for VS Code** і натиснути `F1 → Wokwi: Start Simulator`.
3. Відкрити **Serial Monitor** (швидкість: `115200`).
4. Кожні 10 с у Serial з'являється рядок `[MQTT] Публікуємо: ...` з підтвердженням від брокера.

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

### Книги

- **"Internet of Things with ESP32"** — Agus Kurniawan
  Практичні приклади Wi-Fi, HTTP, MQTT на ESP32.

- **"Making Embedded Systems"** — Elecia White
  Мережева взаємодія, протоколи та надійність з'єднань.