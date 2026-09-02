# Лекція 8 — MQTT Subscriber + Actuator (ESP32-B)

Демонстрація підписки ESP32 на MQTT топік і керування LED актуатором на основі отриманих даних. Callback функція автоматично спрацьовує при кожному вхідному повідомленні. Автоматичний reconnect через `millis()` відновлює з'єднання і підписку без `delay()` (Arduino Framework, PlatformIO + Wokwi).

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
#define LED_PIN        2                          // GPIO2 — актуатор

#define WIFI_SSID      "Wokwi-GUEST"             // SSID мережі (Wokwi симулятор)
#define WIFI_PASSWORD  ""                         // пароль (порожній для Wokwi-GUEST)
#define WIFI_TIMEOUT   10000                      // таймаут підключення, мс

#define MQTT_BROKER    "broker.hivemq.com"        // публічний MQTT брокер
#define MQTT_PORT      1883                       // plain TCP, без TLS
#define MQTT_CLIENT_ID "esp32-demo-b"             // унікальний — не як у ESP32-A!
#define TOPIC_SENSORS  "iot-course/demo/sensors"  // топік на який підписуємось

#define RECONNECT_INTERVAL 5000                   // інтервал між спробами reconnect, мс
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
        // підключено — одразу підписуємось на топік
        mqttClient.subscribe(TOPIC_SENSORS);
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
    // парсимо температуру через strstr + atof
    // керуємо LED на основі значення
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
```

### 6. Неблокуючий reconnect

```cpp
void loop() {
    if (mqttClient.connected()) {
        mqttClient.loop();  // підтримує Keep Alive і викликає callback
    } else {
        if ((now - lastReconnectAttempt) > RECONNECT_INTERVAL) {
            connectMQTT();  // спроба reconnect раз на 5 секунд
        }
    }
}
```

`mqttClient.loop()` викликається тільки коли підключено — без нього callback не спрацьовує і брокер не отримує PING.

---

## Вивід у Serial

```
ESP32-B старт
[Wi-Fi] Підключаємось.... OK
[Wi-Fi] IP: 10.13.37.2
[MQTT] Підключаємось до broker.hivemq.com... OK
[MQTT] Підписались на: iot-course/demo/sensors
[MQTT] Топік: iot-course/demo/sensors
[MQTT] Payload: {"temperature":27.0,"humidity":55.0}
[MQTT] Температура: 27.00
[LED] ON — вище 26°C
[MQTT] Топік: iot-course/demo/sensors
[MQTT] Payload: {"temperature":19.0,"humidity":55.0}
[MQTT] Температура: 19.00
[LED] OFF — нижче 20°C
```

При втраті з'єднання:

```
[MQTT] З'єднання втрачено — перепідключаємось...
[MQTT] Підключаємось до broker.hivemq.com... OK
[MQTT] Підписались на: iot-course/demo/sensors
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