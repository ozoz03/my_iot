# Лекція 8 — MQTT Subscriber + Actuator (ESP32-B) — ДЗ4

Підписка ESP32 (`subscribe(..., 1)`, фактична доставка QoS 0 — деталі нижче) на топік сенсорів і топік команд ESP32-A, керування LED актуатором на основі температури та на команду `"manual_read"` (неблокуюче блимання 3 рази). Callback функція автоматично спрацьовує при кожному вхідному повідомленні. Автоматичний reconnect через `millis()` відновлює з'єднання і підписки без `delay()`: циклами по 3 спроби з паузою 60 с між циклами, і окремо обробляє відсутність Wi-Fi (Arduino Framework, PlatformIO + Wokwi).

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

#define TOPIC_SENSOR_TEMP  "iot-course/ozoz03/sensors/temperature"  // підписка (QoS 1) — лише температура
#define TOPIC_COMMANDS     "iot-course/ozoz03/commands"              // підписка (QoS 1) — команди з ESP32-A
#define TOPIC_ACTUATOR_LED "iot-course/ozoz03/actuators/led"         // публікація поточного стану LED (retained)
#define TOPIC_STATUS       "iot-course/ozoz03/status/esp32-b"        // online/offline (retained, LWT)
#define SUBSCRIBE_QOS  1

#define RECONNECT_INTERVAL          5000             // інтервал між спробами reconnect, мс
#define MQTT_MAX_RECONNECT_ATTEMPTS 3                // максимум спроб reconnect поспіль
#define RECONNECT_COOLDOWN          60000            // пауза перед новим циклом спроб, мс
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
    // LWT: якщо з'єднання обірветься нештатно, брокер сам опублікує
    // "offline" (retained) у TOPIC_STATUS
    if (mqttClient.connect(MQTT_CLIENT_ID, TOPIC_STATUS, 1, true, "offline")) {
        mqttClient.publish(TOPIC_STATUS, "online", true);  // retained
        // підключено — одразу підписуємось на обидва топіки з QoS 1
        mqttClient.subscribe(TOPIC_SENSOR_TEMP, SUBSCRIBE_QOS);
        mqttClient.subscribe(TOPIC_COMMANDS, SUBSCRIBE_QOS);
        mqttClient.publish(TOPIC_ACTUATOR_LED, "OFF", true);  // стартовий стан LED
    }
}
```

Підписка викликається всередині `connectMQTT()`, а не в `setup()`. Причина: `Clean Session = true` скидає всі підписки при кожному відключенні. Так підписка автоматично відновлюється після кожного reconnect.

- `setKeepAlive(60)` — ESP32 надсилає PING брокеру кожні 60 секунд.
- `setSocketTimeout(30)` — таймаут TCP сокету 30 секунд.
- `setCallback(onMessage)` — реєструється до `connectMQTT()`, в `setup()`.
- LWT (`willTopic`/`willMessage`) — брокер сам публікує `"offline"` у `TOPIC_STATUS` при нештатному розриві з'єднання.
- **`subscribe(..., SUBSCRIBE_QOS)` не означає гарантовану доставку QoS 1.** Брокер доставляє повідомлення з мінімумом {QoS публікації, QoS підписки}. ESP32-A публікує через `PubSubClient::publish()`, який підтримує лише QoS 0 (параметра QoS у нього немає) — тож фактична доставка на B завжди QoS 0, незалежно від того, що тут стоїть `subscribe(topic, 1)`.

### 3. Callback функція — `onMessage(topic, payload, length)`

```cpp
void onMessage(char* topic, byte* payload, unsigned int length) {
    static char message[128];             // фіксований буфер, не VLA на стеку
    if (length >= sizeof(message)) return; // задовгий payload — ігноруємо
    // byte* → char* (додаємо термінатор '\0')
    // якщо topic == TOPIC_COMMANDS і payload == "manual_read":
    //   Serial.println("Manual trigger received") + startBlink()
    // якщо topic == TOPIC_SENSOR_TEMP:
    //   atof(message) напряму — топік уже містить лише температуру, керуємо LED
}
```

Правила callback:
- `payload` — масив байтів без термінатора. Завжди конвертувати через `memcpy` + `'\0'`.
- Буфер під payload — фіксованого розміру (`static char message[128]`), не `char message[length + 1]` на стеку. `length` приходить від чужого повідомлення на публічному брокері й нічим не обмежений із нашого боку; на поточному `MQTT_MAX_PACKET_SIZE` (256, `PubSubClient.h`, `setBufferSize()` у B не викликається) стек і так у безпеці, але перевірка `length >= sizeof(message)` — це страховка на майбутнє, якщо буфер MQTT колись підняти до кількох кілобайт.
- Не використовувати `delay()` всередині — блокує `loop()`.
- `mqttClient.publish()` всередині callback тут допустимий і навмисний — саме так B публікує свій новий стан у `TOPIC_ACTUATOR_LED` одразу після зміни LED, без окремого виклику десь у `loop()`.
- Викликається автоматично всередині `mqttClient.loop()`.

### 4. Читання значення — `atof()`

```cpp
float temperature = atof(message);
```

Оскільки ESP32-A публікує температуру в окремий підтопік `sensors/temperature` як просте число (не JSON), парсинг зведений до прямого `atof()` — без `strstr()` і пошуку ключа.

### 5. Логіка керування LED

```cpp
// Цільовий стан оновлюється порогами ЗАВЖДИ, навіть під час активного blink —
// так після blink LED відповідає останній відомій температурі
if (temperature > 26.0) {
    ledStateBeforeBlink = true;
} else if (temperature < 20.0) {
    ledStateBeforeBlink = false;
}
// між 20°C і 26°C — мертва зона, ledStateBeforeBlink лишається без змін

if (blinkActive) return;  // сам пін під час blink не чіпаємо

bool ledOn = digitalRead(LED_PIN);
if (ledStateBeforeBlink != ledOn) {
    digitalWrite(LED_PIN, ledStateBeforeBlink);
    mqttClient.publish(TOPIC_ACTUATOR_LED, ledStateBeforeBlink ? "ON" : "OFF", true);  // retained
}
```

Порівняння з `ledOn` не дає повторно публікувати той самий стан у `TOPIC_ACTUATOR_LED` при кожному вхідному повідомленні — лише коли LED справді має змінити стан.

### 6. Неблокуюче блимання на `"manual_read"` — `startBlink()` / `handleBlink()`

`onMessage()` лише виставляє прапорці (`blinkActive`, `blinkTogglesLeft`) — сам blink виконується в `loop()` через `handleBlink()`, що перемикає `LED_PIN` кожні `BLINK_INTERVAL` мс без `delay()`. Після `BLINK_TOGGLE_COUNT` (6) перемикань — це рівно 3 повних ON/OFF — LED гарантовано завершує послідовність.

Дві важливі деталі, яких не було в першій версії:

- **Блимання йде від програмного рівня `blinkLevel`, а не від фізичного стану піна.** Якщо раніше `handleBlink()` читав `digitalRead(LED_PIN)` і інвертував його, то при старті blink з увімкненого LED перше перемикання його гасило, а не запалювало — видно було 2 спалахи замість 3. Тепер `startBlink()` завжди скидає `blinkLevel = false`, і послідовність незмінно йде LOW→HIGH→LOW→HIGH→LOW→HIGH — рівно 3 повних цикли, незалежно від того, яким був LED до blink.
- **Після blink LED повертається не в жорсткий `LOW`, а в `ledStateBeforeBlink`** — логічний стан, актуальний за останньою температурою. Він оновлюється в `onMessage()` за порогами навіть поки blink ще триває (просто не чіпаючи сам пін, доки `blinkActive`). Приклад старого бага: LED світився (27°C), прийшов `manual_read` → раніше LED після blink примусово гас і лишався вимкненим до наступного показника (до 10 с), а якщо наступна температура потрапляла в мертву зону (20–26°C) — назавжди, доки хтось не пришле >26°C. Тепер `ledStateBeforeBlink` знімається на старті blink і донакопичується новими показниками під час нього, тож після blink LED одразу відповідає дійсності.

### 7. Неблокуючий reconnect з циклами спроб — `maintainConnection()`

```cpp
void loop() {
    handleBlink();

    if (mqttClient.connected()) {
        mqttClient.loop();  // підтримує Keep Alive і викликає callback
        mqttReconnectAttempts = 0;
        mqttReconnectExhausted = false;
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

`mqttClient.loop()` викликається тільки коли підключено — без нього callback не спрацьовує і брокер не отримує PING.

`maintainConnection()` розділяє дві причини відсутності з'єднання:
- **Немає Wi-Fi** — спроба MQTT тут завідомо марна, тож лічильник спроб не витрачається: замість цього неблокуюче `WiFi.reconnect()`.
- **Wi-Fi є, брокер не відповідає** — це і є "справжня" спроба reconnect, вона враховується в лічильнику.

Після `MQTT_MAX_RECONNECT_ATTEMPTS` невдалих спроб поспіль пристрій не зависає офлайн назавжди: чекає паузу `RECONNECT_COOLDOWN` (60 с), скидає лічильник і починає новий цикл спроб. У `setup()` результат `connectWifi()` перевіряється — якщо Wi-Fi одразу не піднявся, `connectMQTT()` там навіть не викликається, і `maintainConnection()` бере це на себе з першої ж ітерації `loop()`.

---

## Вивід у Serial

```
ESP32-B старт
[Wi-Fi] Підключаємось.... OK
[Wi-Fi] IP: 10.13.37.2
[MQTT] Підключаємось до broker.hivemq.com... OK
[MQTT] Підписались на: iot-course/ozoz03/sensors/temperature та iot-course/ozoz03/commands
[MQTT] Топік: iot-course/ozoz03/sensors/temperature
[MQTT] Payload: 27.0
[MQTT] Температура: 27.00
[LED] ON — вище 26°C
[MQTT] Топік: iot-course/ozoz03/commands
[MQTT] Payload: manual_read
Manual trigger received
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