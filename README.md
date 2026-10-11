# Лекція 15 — OTA: оновлення прошивки по повітрю через AWS IoT Jobs

Прошивка ESP32, яка вміє **оновити саму себе**: AWS IoT Jobs доставляє документ роботи по MQTT, пристрій качає новий образ по HTTPS з S3, пише його в вільний OTA-слот і перезавантажується вже в нову версію. Плюс усе з Заняття 14 — телеметрія вгору, команди вниз.

Розвиток Заняття 14: канали «вгору» і «вниз» лишаються без змін, додається модуль `ota/`, дві підписки на зарезервовані топіки `$aws/things/<Thing>/jobs/...`, таблиця розділів із двома слотами застосунку і `version.h` як єдине джерело правди про версію.

> **Якщо OTA зламався** — розділ [OTA у Wokwi: де воно ламається](#ota-у-wokwi-де-воно-ламається). Перше питання там не «що написано в Serial», а «в який момент це сталось».

---

## Архітектура

```
Браузер (HTML/) ──POST /command──► FastAPI ──publish──► AWS IoT Core
                                                             │
                                        topic: .../commands/led
                                                             ▼
                                                          ESP32  → LED
                                                             │
                                        topic: .../telemetry │
                                                             ▼
                                              Rules Engine → DynamoDB

           S3 (firmware.bin + job.json)
                    │        │
   presigned URL ◄──┘        └──► AWS IoT Jobs
          │                              │
          │        $aws/things/<Thing>/jobs/notify-next
          │        $aws/things/<Thing>/jobs/$next/get/accepted
          │                              ▼
          └────────HTTPS GET───────►  ESP32 → Update.write() → app1 → reboot
```

---

## Структура проєкту

```
src/
├── main.cpp           — setup/loop, таймер публікації, підтвердження образу
├── version.h          — FIRMWARE_VERSION — єдине джерело правди про версію
├── net/               — Wi-Fi + NTP
│   ├── net.h
│   └── net.cpp
├── mqtt/              — TLS, connect/subscribe, publish, reconnect, callback
│   ├── mqtt.h         —   + топіки робіт і mqtt_take_job()
│   └── mqtt.cpp
├── ota/               — розбір документа роботи, завантаження, запис у флеш
│   ├── ota.h
│   └── ota.cpp
├── led/               — led1 (D2), розбір команди
│   ├── led.h
│   └── led.cpp
├── dht/               — dht22 (D4)          ЗАГЛУШКА
├── button/            — btn1 (D5)           ЗАГЛУШКА
├── ldr/               — ldr1 (D14 / D34)    ЗАГЛУШКА
├── secrets.h          — Wi-Fi, endpoint і сертифікати (У .gitignore!)
└── secrets.example.h  — шаблон secrets.h для копіювання
partitions.csv         — два слоти застосунку: без цього OTA нікуди писати
merge_firmware.py      — post-скрипт: склеює merged.bin для Wokwi
job.json               — документ роботи, кладеться в S3 (див. job.json.README.md)
diagram.json           — схема підключення для Wokwi-симулятора
wokwi.toml             — конфігурація Wokwi
platformio.ini         — конфігурація PlatformIO
```

PlatformIO збирає `src/` рекурсивно — окремих налаштувань для підпапок не треба. Заглушки містять тільки піни й коментар «сюди йде код по…»; компіляції не заважають.

---

## Налаштування secrets.h

`secrets.h` містить приватний ключ і **до git не потрапляє** (у `.gitignore`).

1. Скопіювати `src/secrets.example.h` → `src/secrets.h`.
2. Вписати `THINGNAME` (= ім'я Thing, = `device_id`) та `AWS_IOT_ENDPOINT`
   (AWS IoT Console → Settings → Device data endpoint).
3. Вставити вміст трьох `.pem` файлів, завантажених у Занятті 9:
   - `AmazonRootCA1.pem` → `AWS_CERT_CA` — перевірка сервера («це справді AWS?»)
   - `certificate.pem.crt` → `AWS_CERT_CRT` — паспорт пристрою («ось хто я»)
   - `private.pem.key` → `AWS_CERT_PRIVATE` — секретний доказ («паспорт справді мій»)

> **Client ID має дорівнювати `THINGNAME`** — AWS Policy обмежує Connect саме по ньому (слайд 16).

**Policy має дозволяти не тільки Publish.** Для команд потрібні ще `iot:Subscribe` і `iot:Receive` на `iot-course/demo/commands/led`. Без них `connect()` пройде, а підписка мовчки не вдасться.

**Для OTA до Policy додаються топіки робіт** — `$aws/things/<Thing>/jobs/*`: `iot:Subscribe` і `iot:Receive` на `notify-next` та `$next/get/accepted`, `iot:Publish` на `$next/get` і `<jobId>/update`. Без прав на `update` пристрій оновиться, але роботу не закриє — і качатиме її по колу.

Окремо потрібна **presigning-роль** із `s3:GetObject` на бакет із прошивкою — це вона перетворює плейсхолдер у `job.json` на робоче посилання (див. [job.json.README.md](job.json.README.md)).

---

## Залежності

```ini
lib_deps =
    adafruit/DHT sensor library
    knolleary/PubSubClient
    bblanchon/ArduinoJson
```

`WiFi.h`, `WiFiClientSecure.h`, `HTTPClient.h`, `Update.h` і `Preferences.h` входять до складу ESP32 Arduino core — додаткових бібліотек ні для TLS, ні для OTA не потрібно.

**Версія ядра має значення.** У `platformio.ini` стоїть `platform = espressif32` без версії, тож PlatformIO бере найновішу встановлену платформу. Проєкт зібрано на **Arduino core 3.3.11 (ESP-IDF 5.5)**. Перевірити, що саме зібралось, — перші рядки виводу `pio run`:

```
PLATFORM: Espressif 32 (...)
 - framework-arduinoespressif32 @ 3.3.11
```

Три місця в коді існують саме через ядро 3.x: `#include <WiFi.h>` в `ota.cpp` (без нього `WiFiClient` не оголошено), `esp_task_wdt_reconfigure()` (у ядрі 2.x такої функції немає) і докачка хвоста файлу (див. [Момент 1](#момент-1-завантаження-і-запис)).

**`build_flags` з `CONFIG_ESP_*` тут не працюють.** `framework = arduino` постачається **вже зібраним**, `sdkconfig` зафіксовано всередині бібліотек. Рядок на кшталт `-DCONFIG_ESP_INT_WDT_TIMEOUT_MS=3000` компілюється без помилки і не робить нічого — виглядає як налаштування, а насправді ні. Watchdog налаштовується тільки в рантаймі (`esp_task_wdt_reconfigure()`).

Подивитись, що ядро насправді вміє:

```
C:\Users\<user>\.platformio\packages\framework-arduinoespressif32-libs\esp32\sdkconfig
```

Це не цікавинка: саме звідти видно `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (випробувальний термін для нового образу, див. [Момент 2](#момент-2-старт-нової-прошивки)) і таймаути обох watchdog.

---

## Опис модулів

### 1. `net/` — Wi-Fi та час

`net_wifi_connect()` — без змін із Заняття 8. Канал 6 (`WiFi.begin(..., 6)`) пропускає сканування, економить ~4 секунди в Wokwi. Повертає `false` при таймауті `WIFI_TIMEOUT`.

`net_time_sync()`:

```cpp
configTime(0, 0, "pool.ntp.org");  // зсув 0, DST 0 — для TLS достатньо
```

**Без цього TLS впаде**, навіть із правильними сертифікатами: ESP32 стартує з 1970 року, і handshake вважає сертифікат AWS «ще не дійсним». Час треба синхронізувати **до** `connect()`.

### 2. `mqtt/` — підключення, підписка, публікація

`mqtt_begin()` — порядок кроків критичний:

```cpp
net_wifi_connect();                     // 1. Wi-Fi
net_time_sync();                        // 2. час (до сертифікатів!)
net.setCACert(AWS_CERT_CA);             // 3. три файли зі слайда 10
net.setCertificate(AWS_CERT_CRT);
net.setPrivateKey(AWS_CERT_PRIVATE);
mqttClient.setServer(AWS_IOT_ENDPOINT, 8883);
mqttClient.setBufferSize(3072);         // 512 замало для документа роботи
mqttClient.setCallback(onMessage);      // ДО connect() — щоб не проґавити
```

Буфер підняли з 512 (Заняття 14) до 3072 саме через OTA — деталі в розділі про топіки робіт.

`WiFiClientSecure net` і `PubSubClient mqttClient` — `static` усередині `mqtt.cpp`. Назовні модуль віддає лише функції, TLS-клієнта ніхто інший не чіпає.

### 3. MQTT Connect + Subscribe — `mqtt_connect()`

```cpp
if (mqttClient.connect(THINGNAME)) {        // Client ID = THINGNAME (слайд 16)
    mqttClient.subscribe(TOPIC_CMD, 1);     // QoS 1 — команду повторити нікому
}
```

`subscribe()` стоїть **усередині** `if` — і це не косметика. Підписка живе в сесії брокера, не на ESP32. Реконект = нова сесія = нуль підписок. Тут вона відновлюється автоматично, бо є частиною підключення, а не окремою дією.

Телеметрію женемо QoS 0: наступний пакет перекриє втрачений.

Коди `mqttClient.state()` при невдачі:
- `-2` — помилка TLS/handshake → перевір **час** і **endpoint**
- `5` — відмовлено в доступі → перевір **AWS Policy**

### 4. Callback — `onMessage()`

```cpp
memcpy(commandBuf, payload, n);
commandBuf[n] = '\0';
commandReady = true;      // роботу зробить loop()
```

Правило: **скопіювати, підняти прапорець, вийти.** Жодної роботи в callback: буфер PubSubClient спільний на вхід і вихід, а поки ми всередині — не летить PING і брокер рахує нас мертвими.

Дві пастки:
- обрізаємо по `sizeof(commandBuf)`, а **не** `sizeof(payload)` — `payload` це вказівник, `sizeof` від нього дасть 4;
- `payload` приходить **без** нуль-термінатора, його ставимо самі.

`commandReady` — `volatile`: пише callback, читає `loop()`.

### 5. `led/` — виконання команди

Викликається з `loop()`, не з callback. Очікуваний payload:

```json
{"action":"set","value":"on"}
```

```cpp
if (strstr(cmd, "\"on\"") != NULL)  digitalWrite(LED, HIGH);
else if (strstr(cmd, "\"off\"") != NULL) digitalWrite(LED, LOW);
```

Шукаємо **значення в лапках**, а не пару `"ключ":"значення"` — тоді пробіли після двокрапки не мають значення. І `"on"` не збігається всередині `"off"`: лапки роблять токени різними.

### 6. Публікація — `mqtt_publish_telemetry()`

`snprintf()` замість Arduino `String` — уникаємо фрагментації heap (Заняття 4). `received_at` тут НЕМА — його дописує правило в хмарі.

```json
{"device_id":"esp32lecture10","timestamp":1784301652,"temperature":27.0,"humidity":55.0,"firmware_version":"1.0.0"}
```

`firmware_version` додано на цьому занятті і береться з `version.h`. Після OTA це **єдиний доказ у хмарі**, що оновлення справді відбулось — і єдиний спосіб побачити, що частина парку відкотилась назад.

### 7. Топіки робіт — `mqtt.cpp`

Топіки **зарезервовані AWS**, ми їх не вигадуємо. `$next` — службове слово «наступна робота в черзі», знати `jobId` наперед не треба:

```cpp
#define TOPIC_JOBS_NOTIFY  "$aws/things/" THINGNAME "/jobs/notify-next"
#define TOPIC_JOBS_GET     "$aws/things/" THINGNAME "/jobs/$next/get"
#define TOPIC_JOBS_ACCEPT  "$aws/things/" THINGNAME "/jobs/$next/get/accepted"
```

Callback став диспетчером на три види топіка. На `notify-next` нічого не копіюємо — одразу публікуємо в `get`. На `get/accepted` копіюємо документ у власний буфер і піднімаємо прапорець.

Дві речі, які ламають OTA мовчки:

- **`setBufferSize(3072)`.** Документ роботи містить presigned URL — це сотні байтів. У 512 він не влізе, PubSubClient обріже його **без жодного повідомлення**, а ArduinoJson поверне `IncompleteInput`. Виглядає як баг парсера, а це буфер.
- **`mqtt_request_next_job()` усередині `mqtt_connect()`.** Поки пристрій був офлайн, роботу могли створити — `notify-next` тоді вже не прилетить. Її треба забрати самим при кожному підключенні.

### 8. `ota/` — розбір, завантаження, запис

`ota_handle_job()` викликається з `loop()` і робить чотири речі по черзі: розбирає JSON, порівнює версії, качає, звітує.

**Що перевіряє ArduinoJson — і чого не перевіряє.** `deserializeJson()` відповідає лише на питання «чи це цілий, синтаксично правильний JSON»: `IncompleteInput` — документ обрізано, `InvalidInput` — зіпсовано, `NoMemory` — не влізло. Решту перевіряє наш код: є обʼєкт `execution` (інакше робіт немає), є рядки `jobId`, `version`, `url` (відсутнє поле або не-рядок дає `NULL`). Ні формат версії, ні адресу, ні сам файл прошивки ArduinoJson не перевіряє — про файл див. нижче.

**Порівняння версій — це і є закриття роботи.** Після оновлення пристрій піднімається, знову питає роботу, бачить `version == FIRMWARE_VERSION` і звітує `SUCCEEDED`. Без цієї перевірки нова прошивка почала б качати саму себе по колу.

Завантаження розбите на три функції:

- `open_stream()` — HTTPS GET. З позиції 0 чекає `200`; для докачки шле `Range: bytes=N-` і чекає `206`.
- `pump_stream()` — ручний цикл шматками по 4 КБ з потоку в `Update.write()`.
- `download_and_flash()` — диригент: `Update.begin()`, качання, до трьох докачок, `Update.end(true)`.

**Вручну шматками, не `Update.writeStream()`.** У arduino-esp32 (issue #9997) `writeStream` помилково завершується при `read() == 0` посеред потоку: великий TLS-потік з S3 віддається нерівними шматками, і бібліотека вирішує, що потік мертвий → «Записано 0».

**`setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS)`** — S3 відповідає 301/302 на регіональний хост. Без цього потік порожній.

**Що перевіряє сам `Update`.** Перший байт образу має бути `0xE9` — інакше `Update` вважає файл зашифрованим і падає з `Decryption error`. `Update.end(true)` перевіряє SHA-256, дописаний у кінець образу. Це захист від **пошкодженого** файлу, не від **підробленого**: хеш лежить у тому самому файлі, і хто підмінив файл, підмінить і хеш. А `download_and_flash()` викликає `client.setInsecure()` — сертифікат S3 не перевіряється. Від зловмисника між пристроєм і S3 цей демо-код не захищає; для курсового проєкту — `setCACert()` на HTTPS-клієнті (див. Заняття 17).

**Невдале оновлення повторюється нескінченно.** Кожен `mqtt_connect()` питає роботу заново, тому ресет посеред оновлення означає «качаємо те саме ще раз». Якщо образ у бакеті непридатний, пристрій ходитиме по цьому колу, доки роботу не скасують у хмарі. У продакшені сюди ставлять лічильник спроб у NVS (рахувати треба **до** завантаження — ресет від watchdog не дасть порахувати після); у демо його немає навмисно, щоб не ховати сам цикл.

### 9. `loop()` — хто кого смикає

```cpp
if (otaInProgress) return;                    // під час OTA більше нічого

if (!mqtt_connected()) {
    if (imagePending && millis() > CONFIRM_TIMEOUT_MS)
        esp_ota_mark_app_invalid_rollback_and_reboot();   // нова прошивка не робоча
    mqtt_reconnect_tick();
    return;
}

if (imagePending) {                           // MQTT є — підтверджуємо образ
    esp_ota_mark_app_valid_cancel_rollback();
    imagePending = false;
}

mqtt_poll();                                  // кожну ітерацію
const char* cmd = mqtt_take_command();        // NULL, якщо команди нема
if (cmd) led_handle_command(cmd);

const char* job = mqtt_take_job();
if (job) { ota_handle_job(job); return; }     // при успіху звідси не вийдемо
```

`mqtt_poll()` **обовʼязково кожну ітерацію**: читає вхідні байти й тримає Keep Alive. Без нього ні команди, ні роботи не приходять узагалі.

`mqtt_reconnect_tick()` — раз на 5 секунд без `delay()`: спершу піднімає Wi-Fi (без мережі реконект MQTT безнадійний), потім `net.stop()` — інакше mbedTLS-контекст лишається «напівживим» і з часом зʼїдає купу — і аж тоді `mqtt_connect()`.

Звідки `imagePending` і чому підтвердження саме тут — [Момент 3](#момент-3-нова-прошивка-стартувала-але-не-працює).

---

## OTA у Wokwi: де воно ламається

OTA може зламатись у трьох різних моментах, і в кожного свої наслідки та свої ліки. Тому перше питання при будь-якому збої — не «що написано», а **«коли це сталось»**. Відповідь дають два рядки:

- **останній рядок, який надрукувала програма** до збою;
- **рядок `rst:0x… (…)`**, який друкує ROM чипа при старті, — причина ресету.

Життя нового образу в завантажувачі (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`):

```
Update.end(true)        → слот NEW
завантажувач            → бачить NEW, ставить PENDING_VERIFY і запускає
підтвердження           → VALID, образ прийнято назавжди
ресет до підтвердження  → ABORTED, слот викреслено, вантажимо старий
```

### Момент 1. Завантаження і запис

Працює ще **стара** прошивка, новий слот не активовано. Тому збій тут **нешкідливий**: пристрій лишається на старій версії, а роботу буде повторено при наступному підключенні.

**`yield()` не годує watchdog.** У довгому циклі завантаження `yield()` виглядає правильно — і не робить нічого корисного. На ESP32 `yield()` перемикає лише задачі **того самого пріоритету**. `loopTask` має пріоритет 1, `IDLE` — 0, тому IDLE так і не отримує час і не скидає Task WDT.

```cpp
delay(1);   // блокує задачу на тік → IDLE прокидається → WDT нагодований
```

**Довга операція — довший watchdog.** Запис ~1 МБ і перевірка SHA в `Update.end()` довші за стандартні 5 с Task WDT. На час OTA таймаут подовжується, а не вимикається:

```cpp
esp_task_wdt_config_t wdtOta = { 60000, 1, true };      // 60 с, стежимо за IDLE0, паніка
esp_task_wdt_reconfigure(&wdtOta);
bool ok = download_and_flash(url);
esp_task_wdt_config_t wdtNormal = { CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000, 1, true };
esp_task_wdt_reconfigure(&wdtNormal);
```

Чому не `disableCore0WDT()`: у ядрі 3.x хук IDLE0 після відписки далі пробує годувати watchdog, і кожна спроба — рядок `task_wdt: esp_task_wdt_reset(707): task not found`, сотні на секунду. Вони забивають Serial і гальмують завантаження.

**ESP32 має два watchdog — не плутайте.** Task WDT (група таймерів TG0, 5 с) стежить, щоб задачі віддавали час IDLE. Interrupt WDT (TG1, 300 мс) стежить, щоб на кожному ядрі тікав планувальник — тобто щоб переривання не були вимкнені надовго. Запис у флеш якраз вимикає переривання: поки ESP-IDF працює з флешем, кеш вимкнено, а друге ядро чекає. Якщо в Wokwi на цьому етапі зʼявився `TG1WDT_SYS_RESET` — це interrupt watchdog, і налаштування Task WDT вище на нього не впливають.

**Обрізаний хвіст файлу (ядро 3.x).** Коли сервер закриває TCP-зʼєднання, `NetworkClientSecure::available()` отримує від mbedTLS помилку `-76`, робить `stop()` — і викидає байти, які вже прийшли й лежать у буфері TLS. Файл обривається за кілька КБ до кінця, щоразу на тому самому місці. `download_and_flash()` відкриває зʼєднання ще раз і просить лише хвіст — `Range: bytes=N-` — у ту саму Update-сесію. Якщо сервер віддав не рівно те, чого бракує, — `Update.abort()`: писати в слот чужі байти не можна.

**Як упізнати момент 1:** збій або `[OTA] Не вдалось` **до** рядка `[OTA] Готово. Перезавантаження...`.

### Момент 2. Старт нової прошивки

`[OTA] Готово. Перезавантаження...` надруковано, чип стартує — і жодного рядка з `setup()`. Нова прошивка зависла на старті.

У Wokwi це відтворювалось після `ESP.restart()`. Допомагає рестарт через пробудження з deep sleep:

```cpp
esp_sleep_enable_timer_wakeup(100000);   // 100 мс
esp_deep_sleep_start();
```

Пробудження — це ресет процесорів і цифрової периферії: чип стартує майже з нуля. **Майже** — бо RTC-памʼять під час deep sleep живе, і змінні `RTC_DATA_ATTR` нова прошивка отримає від старої (Заняття 16). Це обхід специфіки Wokwi; на реальній платі `ESP.restart()` — стандартний спосіб завершити OTA.

**Страховка:** образ на цей момент ще не підтверджено. Зависла прошивка → ресет → завантажувач бачить `PENDING_VERIFY` без підтвердження → `ABORTED` → пристрій на старій версії. Це rollback у дії, і він спрацює сам.

**Як упізнати момент 2:** після `Готово. Перезавантаження...` — рядок `rst:`, жодного виводу прошивки, а на наступному старті `розділ app0` і стара версія.

### Момент 3. Нова прошивка стартувала, але не працює

Найпідступніший, бо нічого не падає. Нова версія стартувала, у Serial чисто — але пристрій не підключається до MQTT.

**Пастка:** Arduino за замовчуванням підтверджує новий образ **сам, ще до `setup()`**, у `initArduino()`. «Дожив до `setup()`» = «прийнято назавжди». Rollback тоді рятує лише від прошивки, що падає на старті. Прошивка, яка стартує, але не може підключитись, буде підтверджена — і пристрій у серверній чи на даху більше ніколи не отримає наступне OTA: воно приходить через MQTT.

Тому рішення забираємо собі:

```cpp
extern "C" bool verifyRollbackLater() {
    return true;   // «не підтверджуй, я вирішу сам»
}
```

`extern "C"` обовʼязковий. Ядро оголошує цю функцію в C-файлі як «слабку» (weak). Без `extern "C"` компілятор C++ дасть їй інше імʼя, ядро її не побачить і мовчки підтвердить образ саме — жодної помилки не буде.

Далі в `main.cpp`:

- `setup()` перевіряє, чи образ у стані `PENDING_VERIFY`, і ставить `imagePending`. Після прошивки через Wokwi чи кабель образ не на випробуванні — нічого не відбувається.
- `loop()` підтверджує образ **після першого успішного підключення до MQTT**: доказ життя — саме той канал, через який прийде наступне оновлення.
- Якщо за `CONFIRM_TIMEOUT_MS` (120 с) MQTT так і не зʼявився — `esp_ota_mark_app_invalid_rollback_and_reboot()`. Без цього непідтверджена прошивка відкотилась би лише на наступному ресеті, а його може не бути ніколи: пристрій нескінченно пробує реконект.

Занадто суворий критерій теж шкодить: поки образ не підтверджено, будь-який ресет (навіть випадковий) відкотить робоче оновлення.

**Як упізнати момент 3:** `[OTA] Нова прошивка не підключилась — відкат на попередню`, потім старт на `app0`.

### Окремо: `merged.bin` зібраний не з того, що ви щойно скомпілювали

`merge_firmware.py` вішається post-action на **`$BUILD_DIR/firmware.bin`**. Якщо повісити його на файл, який PlatformIO не збирає (напр. вашу ручну копію `firmware_1_1.bin` для S3), дія просто не спрацює, а `merged.bin` лишиться від попередньої збірки. Ви годинами шукаєте баг у коді, якого в чіпі немає.

**Як упізнати:** у Serial текст, якого у ваших джерелах уже немає. Або перевірити час: `merged.bin` має бути новішим за `firmware.bin` на частки секунди.

### Діагностичний рядок

Один рядок у `setup()` показує, де ми опинились після будь-якого з моментів:

```cpp
Serial.printf("ESP32 — OTA, версія %s, розділ %s, причина ресету %d\n",
              FIRMWARE_VERSION,
              esp_ota_get_running_partition()->label,
              (int)esp_reset_reason());
```

`розділ app1` + `причина ресету 8` (`DEEPSLEEP`) — оновились. `розділ app0` після оновлення — відкотились (момент 2 або 3).

Коди `esp_reset_reason()`: `1` POWERON, `3` SW, `4` PANIC, `5` INT_WDT, `6` TASK_WDT, `8` DEEPSLEEP.

---

## Вивід у Serial

Успішне оновлення від початку до кінця:

```
ESP32 — OTA, версія 1.0.0, розділ app0, причина ресету 1
[Wi-Fi] Підключаємось. OK
[Wi-Fi] IP: 10.13.37.2
[NTP] Синхронізація часу OK
[MQTT] Підключаємось до AWS IoT Core... OK
[MQTT] Підписані на commands/led
[MQTT] Підписані на jobs
[MQTT] Публікуємо: {"device_id":"esp32lecture10",...,"firmware_version":"1.0.0"}
[OTA] Нова версія 1.1.0 (у мене 1.0.0) — починаю
[JOBS] Статус IN_PROGRESS для OTA_DEMO_01
[MQTT] Зʼєднання закрито на час OTA
[OTA] HTTP GET -> 200
[OTA] Content-Length: 1081680
[OTA] Пишемо прошивку...
[OTA] Завантажено байтів: 1081680
[OTA] Готово. Перезавантаження...

rst:0x5 (DEEPSLEEP_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
...
ESP32 — OTA, версія 1.1.0, розділ app1, причина ресету 8
[OTA] Нова прошивка на випробуванні — чекаю MQTT
[Wi-Fi] Підключаємось. OK
...
[MQTT] Підключаємось до AWS IoT Core... OK
[MQTT] Підписані на commands/led
[MQTT] Підписані на jobs
[OTA] Образ підтверджено — оновлення прийнято
[OTA] Я вже на цій версії — робота виконана
[JOBS] Статус SUCCEEDED для OTA_DEMO_01
[MQTT] Публікуємо: {"device_id":"esp32lecture10",...,"firmware_version":"1.1.0"}
```

Ключові рядки — `розділ app1`, `Образ підтверджено` і `firmware_version":"1.1.0"`. Все інше може виглядати правильно і при відкаті. Точний `Content-Length` залежить від вашої збірки.

Якщо зʼєднання обірвалось під кінець файлу — це штатна докачка, не помилка:

```
[OTA] Пишемо прошивку...
[ 62558][E][ssl_client.cpp:41] _handle_error(): [data_to_read():425]: (-76) UNKNOWN ERROR CODE (004C)
[ 62569][E][NetworkClientSecure.cpp:313] available(): Closing connection on failed available check
[OTA] З'єднання закрилось до кінця файлу
[OTA] Обрив на 1078863 з 1081680 — докачуємо, спроба 1
[OTA] HTTP GET -> 206
[OTA] Завантажено байтів: 1081680
[OTA] Готово. Перезавантаження...
```

При помилці:

```
[MQTT] Підключаємось до AWS IoT Core... помилка: -2
[MQTT] ПІДПИСКА НЕ ВДАЛАСЬ
[OTA] HTTP GET -> 403
[OTA] Замало байтів: 512
[OTA] Update.write не прийняв усі байти
Decryption error
[OTA] Не вдалось. Працюємо на старій версії
```

### Шум, який можна ігнорувати

```
E (503) esp_core_dump_flash: No core dump partition found!
E (1568) phy_init: load_cal_data_from_nvs_handle: calibration data MAC check
         failed: expected 00:00:00:00:00:00, found 24:0a:c4:00:01:10
```

Перше — Arduino шукає розділ `coredump`, ми його в `partitions.csv` не описали. Втрачаємо тільки посмертний дамп стека у флеш; сама паніка все одно друкується в Serial. Друге — ESP-IDF кешує калібрування радіо в NVS із привʼязкою до MAC, а Wokwi віддає нульовий MAC. Кеш відкидається, радіо калібрується заново, коштує кілька мілісекунд. На реальному чіпі цього рядка не буде.

Обидва — рівня `E`, обидва нешкідливі. Не витрачайте на них час. Те саме стосується пари `ssl_client ... (-76)` / `Closing connection`, якщо за нею йде `докачуємо`.

---

## Перевірка через AWS IoT Console

1. AWS IoT Console → **MQTT test client**.
2. **Subscribe to a topic** → `iot-course/demo/telemetry` — телеметрія має падати раз на 10 с.
3. **Publish to a topic** → `iot-course/demo/commands/led`, payload:

```json
{"action":"set","value":"on"}
```

4. У Wokwi світлодіод має засвітитись, у Serial — `[CMD] LED увімкнено`.

Так перевіряється саме прошивка, без FastAPI. Не працює тут — далі йти немає сенсу.

Для OTA додатково: **Manage → Jobs → ваша робота** показує стан по кожному пристрою. `IN_PROGRESS`, що висить назавжди, означає, що пристрій почав і не повернувся — шукайте ресет у Serial. `QUEUED`, який ніхто не забирає, — пристрій не підписаний на топіки робіт або Policy не дає `iot:Receive`.

Найдешевша перевірка версії парку — не Jobs, а телеметрія: поле `firmware_version` у `iot-course/demo/telemetry`.

---

## Як запустити

1. Скопіювати `secrets.example.h` → `secrets.h` і заповнити (див. вище).
2. Відкрити проєкт у VS Code з розширенням **PlatformIO**.
3. Для симуляції — розширення **Wokwi for VS Code**, `F1 → Wokwi: Start Simulator`.
4. Відкрити **Serial Monitor** (швидкість `115200`).
5. Кожні 10 с у Serial — рядок `[MQTT] Публікуємо: ...`; команда з консолі або з `HTML/index.html` вмикає LED.

---

## Як випустити нову версію

Порядок важливий: спершу збирається те, що поїде **в бакет**, і аж потім те, що лишиться **в чіпі**. Переплутаєте — заллєте в Wokwi ту саму версію, яку роздаєте, і оновлювати буде нічого.

1. `version.h` → `FIRMWARE_VERSION "1.1.0"`
2. Зібрати. Залити в S3 `.pio/build/esp32dev/firmware.bin` під іменем, яке стоїть у `job.json`.
   **Саме `firmware.bin`, не `merged.bin`.** `merged.bin` починається з bootloader-а (перший байт `0xFF`), а образ застосунку — з `0xE9`. `Update` побачить не той перший байт і впаде з `Decryption error`.
   **Звірте розмір** у консолі S3 з локальним `firmware.bin`. Інший розмір — ви залили не той файл.
3. `version.h` → назад на `"1.0.0"`
4. Зібрати. **Перезапустити** симулятор — Wokwi підхоплює новий `merged.bin` лише при старті. Це «старий» пристрій, який зараз оновиться.
5. Оновити `job.json` (поле `version` = `1.1.0`), покласти в S3, створити роботу.
6. **jobId має бути новий.** Попередню роботу вже закрито як `SUCCEEDED` — пристрій до неї не повернеться.

Перевірка, що оновлення справді втрималось: у Serial має бути `[OTA] Образ підтверджено`. Після цього перезавантажте пристрій ще раз — версія має лишитись новою, `розділ app1`.

**Зупинка симулятора ≠ ресет.** `esp_restart()` чи пробудження всередині симуляції зберігають флеш. Stop/Start у Wokwi заново вантажить `merged.bin` — і ви знову на 1.0.0 в `app0`, результат OTA зник. На реальній платі оновлення переживає вимкнення живлення.

---

## Troubleshooting

| Симптом | Куди дивитись |
|---|---|
| `mqttClient.state()` = -2 | TLS/handshake: перевірити час і endpoint |
| `mqttClient.state()` = 5 | Відмовлено: перевірити Policy та Client ID |
| Connect OK, `ПІДПИСКА НЕ ВДАЛАСЬ` | У Policy немає `iot:Subscribe` / `iot:Receive` на топік команд |
| Телеметрія йде, команди не приходять | Топік у публікації не збігається з `TOPIC_CMD` побуквенно |
| Команди приходили, після реконекту зникли | `subscribe()` винесли з `mqtt_connect()` — підписка не відновилась |
| `[CMD] Невідома команда` | У payload немає `"on"` / `"off"` **у лапках** |
| Команда приходить обрізана | `setBufferSize(512)` або `commandBuf[64]` замалі |
| `'WiFiClient' was not declared` при збірці | Ядро 3.x: бракує `#include <WiFi.h>` в `ota.cpp` |
| `Partition Could Not be Found` | Wokwi залив тільки застосунок — потрібен `merged.bin` |
| `IncompleteInput` від ArduinoJson | `setBufferSize()` менший за документ роботи з presigned URL |
| `HTTP GET -> 403` | Ім'я бакета/файлу в `job.json` або права presigning-ролі |
| `HTTP GET -> 302`, потім 0 байтів | Немає `setFollowRedirects()` |
| `Decryption error` одразу після `Пишемо прошивку...` | У S3 не образ застосунку: `merged.bin` або чужий файл (перший байт ≠ `0xE9`) |
| «Записано 0» посеред потоку | `Update.writeStream()` — замінити ручним циклом |
| `Записано лише N з M`, N щоразу однакове | Ядро 3.x обрізає хвіст — потрібна докачка через `Range` |
| Сотні рядків `task_wdt: ... task not found` | `disableCore0WDT()` на ядрі 3.x — замінити на `esp_task_wdt_reconfigure()` |
| Ресет під час `Пишемо прошивку...` | `yield()` замість `delay(1)`; таймаут Task WDT не подовжено |
| `TG1WDT_SYS_RESET` | Interrupt watchdog: переривання вимкнені > 300 мс. Task WDT тут ні до чого |
| Після `Готово` — `rst:` і жодного рядка з `setup()` | Прошивка зависла на старті; у Wokwi — `ESP.restart()` замість deep sleep |
| Оновились, після ресету знову стара | Образ не підтверджено до ресету — дивитись, чи був `Образ підтверджено` |
| Нова версія стартує, але без MQTT і не відкочується | Arduino підтвердив образ сам: немає `verifyRollbackLater()` або бракує `extern "C"` |
| Качає ту саму версію по колу | Не звітуємо `SUCCEEDED` при збігу версій |
| У Serial текст, якого немає в коді | `merged.bin` від старої збірки, або в S3 прошивка з іншого проєкту |

---

## Ключові факти заняття

- Підписка живе **в сесії брокера**, не на пристрої. Реконект стирає її — тому `subscribe()` частина `connect()`
- У callback — тільки копіювання. Робота в callback ламає Keep Alive і топче спільний буфер
- `payload` приходить без `'\0'`, а `sizeof` від вказівника — це 4, а не розмір даних
- Команда — QoS 1 (повторити нікому), телеметрія — QoS 0 (наступний пакет перекриє)
- `mqttClient.loop()` кожну ітерацію, інакше вхідного каналу просто немає
- Модулі ділять код, а не логіку: `mqtt.cpp` тримає TLS-клієнт приватним і віддає назовні лише функції
- **При збої OTA дивіться, коли він стався**: під час запису, на старті нової прошивки чи після старту. Від моменту залежать і наслідки, і ліки
- **Успішний запис у флеш — це ще не оновлення.** Образ проходить випробувальний термін, і підтверджувати його треба лише тоді, коли він довів, що працює, — після підключення до MQTT
- **Arduino підтверджує образ сам, ще до `setup()`.** Щоб рішення було вашим — `verifyRollbackLater()` з `extern "C"`
- **Два watchdog:** Task WDT (TG0) — задачі віддають час; Interrupt WDT (TG1) — переривання не вимкнені надовго. Ліки в них різні
- **Версія у прошивці = версія в документі роботи.** Збіг версій — це і є `SUCCEEDED`; розбіжність — це вічний цикл завантажень
- SHA у `Update.end()` ловить пошкоджений файл, але не підроблений
- `build_flags` з `CONFIG_ESP_*` при `framework = arduino` мовчки не працюють — ядро вже зібране

---

## Рекомендована література

| Ресурс | Посилання |
|---|---|
| AWS IoT Core — Developer Guide | [docs.aws.amazon.com/iot](https://docs.aws.amazon.com/iot/latest/developerguide/what-is-aws-iot.html) |
| AWS IoT — publish/subscribe policy actions | [docs.aws.amazon.com/iot/…/iot-policy-actions](https://docs.aws.amazon.com/iot/latest/developerguide/iot-policy-actions.html) |
| AWS IoT — MQTT protocol support (QoS) | [docs.aws.amazon.com/iot/…/mqtt](https://docs.aws.amazon.com/iot/latest/developerguide/mqtt.html) |
| AWS IoT — device certificates (mTLS) | [docs.aws.amazon.com/iot/…/x509-client-certs](https://docs.aws.amazon.com/iot/latest/developerguide/x509-client-certs.html) |
| PubSubClient — офіційна документація | [pubsubclient.knolleary.net](https://pubsubclient.knolleary.net) |
| WiFiClientSecure (ESP32 Arduino core) | [github.com/espressif/arduino-esp32/…/WiFiClientSecure](https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFiClientSecure) |
| PlatformIO — структура проєкту та `src_dir` | [docs.platformio.org/…/projectconf](https://docs.platformio.org/en/latest/projectconf/index.html) |
| Wokwi — ESP32 Wi-Fi у симуляторі | [docs.wokwi.com/guides/esp32-wifi](https://docs.wokwi.com/guides/esp32-wifi) |
| AWS IoT Jobs — Developer Guide | [docs.aws.amazon.com/iot/…/iot-jobs](https://docs.aws.amazon.com/iot/latest/developerguide/iot-jobs.html) |
| AWS IoT Jobs — зарезервовані топіки | [docs.aws.amazon.com/iot/…/reserved-topics](https://docs.aws.amazon.com/iot/latest/developerguide/reserved-topics.html#reserved-topics-job) |
| AWS IoT Jobs — presigned URL у документі | [docs.aws.amazon.com/iot/…/create-manage-jobs](https://docs.aws.amazon.com/iot/latest/developerguide/create-manage-jobs.html) |
| ESP-IDF — OTA і rollback (`esp_ota_ops`) | [docs.espressif.com/…/esp_https_ota](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html) |
| ESP-IDF — таблиці розділів | [docs.espressif.com/…/partition-tables](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html) |
| ESP-IDF — watchdog timers | [docs.espressif.com/…/wdts](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html) |
| arduino-esp32 — `Update` (OTA) | [github.com/espressif/arduino-esp32/…/Update](https://github.com/espressif/arduino-esp32/tree/master/libraries/Update) |
| ArduinoJson — `deserializeJson()` і коди помилок | [arduinojson.org](https://arduinojson.org) (пошук: deserializeJson DeserializationError) |
| Random Nerd Tutorials — ESP32 + AWS IoT | [randomnerdtutorials.com/esp32-aws-iot-core](https://randomnerdtutorials.com/esp32-aws-iot-core-mqtt-arduino/) |

### Додаткові матеріали

- **AWS — Using Device Time to Validate AWS IoT Server Certificates** — [aws.amazon.com/blogs/iot/using-device-time-to-validate-aws-iot-server-certificates](https://aws.amazon.com/blogs/iot/using-device-time-to-validate-aws-iot-server-certificates/)
- **AWS — Security best practices in AWS IoT Core** — [docs.aws.amazon.com/iot/latest/developerguide/security-best-practices.html](https://docs.aws.amazon.com/iot/latest/developerguide/security-best-practices.html)
- **PubSubClient (knolleary)** — та сама бібліотека з Заняття 8 — [github.com/knolleary/pubsubclient](https://github.com/knolleary/pubsubclient)
- **WiFiClientSecure** — довідник ESP32 Arduino Core — [docs.espressif.com](https://docs.espressif.com) (пошук: WiFiClientSecure ESP32)
