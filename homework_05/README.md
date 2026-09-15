# Заняття 11 — Обробка даних у хмарі: Rules Engine

Продовження Заняття 10. Пристрій уже підключений до AWS IoT Core через mutual TLS
і публікує телеметрію. Зараз ми ставимо в кінець цієї труби базу даних —
без жодного рядка бекенду.

---

## Архітектура

```
┌──────────────┐
│  ESP32       │  Wokwi
│  (DHT22 sim) │
└──────┬───────┘
       │ MQTT over TLS, порт 8883
       │ топік: iot-course/demo/telemetry
       │ payload: {"device_id","timestamp","temperature","humidity"}
       ▼
┌──────────────────────────────────────┐
│  AWS IoT Core (eu-north-1)           │
│  ┌────────────────────────────────┐  │
│  │  Rules Engine                  │  │
│  │  StoreTelemetry                │  │
│  │  SELECT * + timestamp()        │  │
│  │           + clientid()         │  │
│  │           + topic(2)           │  │
│  └────────────┬───────────────────┘  │
└───────────────┼──────────────────────┘
                │ дія dynamoDBv2
                │ IAM-роль: iot_rule_ddb_role
                ▼
        ┌───────────────────┐
        │  DynamoDB         │
        │  iot_telemetry    │
        │  pk: device_id    │
        │  sk: received_at  │
        └───────────────────┘
                │
                ▼
         Заняття 12-13: FastAPI → Grafana
```

**Ключова ідея:** брокер MQTT нічого не зберігає. Rules Engine живе всередині
брокера, бачить кожне повідомлення і розкладає його по місцях. Це не підписник —
йому байдуже, чи є в топіка слухачі.

---

## Параметри цього проєкту

| Що | Значення |
|---|---|
| Регіон | `eu-north-1` |
| Thing / Client ID / `device_id` | `esp32lecture10` |
| Топік телеметрії | `iot-course/demo/telemetry` |
| Таблиця DynamoDB | `iot_telemetry` |
| Правило IoT | `StoreTelemetry` |
| IAM-роль правила | `iot_rule_ddb_role` |

**Регіон має бути один і той самий скрізь.** Правило в одному регіоні, а таблиця
в іншому — вони не побачать одне одного ніколи, і в помилках буде порожньо.
Перевіряйте правий верхній кут консолі щоразу, коли перемикаєтесь між сервісами.

---

## Структура проєкту

```
src/
├── main.cpp           — основний файл прошивки
├── secrets.h          — Wi-Fi, endpoint і сертифікати (У .gitignore!)
└── secrets.example.h  — шаблон secrets.h для копіювання
diagram.json           — схема підключення для Wokwi-симулятора
wokwi.toml             — конфігурація Wokwi
platformio.ini         — конфігурація PlatformIO
```

---

## Налаштування secrets.h

`secrets.h` містить приватний ключ і **до git не потрапляє** (у `.gitignore`).

1. Скопіювати `src/secrets.example.h` → `src/secrets.h`.
2. Вписати `THINGNAME` (= ім'я Thing, = `device_id`, наприклад `esp32lecture10`) та
   `AWS_IOT_ENDPOINT` (AWS IoT Console → Settings → Device data endpoint).
3. Вставити вміст трьох `.pem` файлів, завантажених у Занятті 9:
   - `AmazonRootCA1.pem` → `AWS_CERT_CA` — перевірка сервера («це справді AWS?»)
   - `certificate.pem.crt` → `AWS_CERT_CRT` — паспорт пристрою («ось хто я»)
   - `private.pem.key` → `AWS_CERT_PRIVATE` — секретний доказ («паспорт справді мій»)

> **Client ID має дорівнювати `THINGNAME`** — AWS Policy обмежує Connect саме по ньому (слайд 16).

---

## Залежності

```ini
lib_deps =
    knolleary/PubSubClient
    adafruit/DHT sensor library
```

`WiFi.h` і `WiFiClientSecure.h` входять до складу ESP32 Arduino core — додаткових бібліотек для TLS не потрібно.

---

## Крок 1. Таблиця DynamoDB

### 1.1 Відкрити консоль

1. Увійти в AWS Console
2. Перевірити регіон у правому верхньому кутку — має бути **eu-north-1**
3. У рядку пошуку зверху ввести `DynamoDB` → відкрити сервіс

### 1.2 Створити таблицю

1. Ліве меню → **Tables**
2. Кнопка **Create table**

Заповнити **Table details**:

| Поле | Значення | Тип |
|---|---|---|
| Table name | `iot_telemetry` | — |
| Partition key | `device_id` | **String** |
| Sort key | `received_at` | **Number** |

**Sort key обовʼязково Number, не String.** Якщо поставити String, сортування буде
лексикографічне — посимвольне, як у словнику. Рядок `"9"` виявиться більшим за `"10"`.
Запит "останні 6 годин" поверне кашу, а Grafana намалює графік, у якому точки
стрибають у часі назад.

### 1.3 Table settings

Лишаємо **Default settings**.

Це означає режим **On-demand**: платимо за кожен запит з першого, зате воно
ніколи не впирається в стелю пропускної здатності. Для одного пристрою, який
шле раз на 10 секунд, це приблизно три центи на місяць.

Для довідки: у DynamoDB є безкоштовні 25 WCU / 25 RCU, але вони існують тільки
в режимі **Provisioned**. Ціни й ліміти змінюються — перевіряйте на
https://aws.amazon.com/dynamodb/pricing/

### 1.4 Створити

**Create table** → чекаємо, поки статус стане **Active** (10–20 секунд).

### 1.5 Що не можна змінити після створення

| Налаштування | Змінюється потім? |
|---|---|
| Partition key | **Ні. Ніколи** |
| Sort key | **Ні. Ніколи** |
| Тип ключа (String / Number) | **Ні. Ніколи** |
| Capacity mode (On-demand / Provisioned) | Так |
| Table class | Так |
| Deletion protection | Так |

Помилилися з ключем — тільки видалити таблицю і створити нову.

---

## Крок 2. Правило IoT (Rules Engine)

### 2.1 Відкрити

1. Перевірити регіон — **eu-north-1**
2. Сервіс **AWS IoT Core**
3. Ліве меню → **Manage** → **Message routing** → **Rules**
4. Кнопка **Create rule**

### 2.2 Rule properties

| Поле | Значення |
|---|---|
| Rule name | `StoreTelemetry` |
| Rule description | `Telemetry to DynamoDB` (опційно) |

В імені правила дозволені тільки літери, цифри і підкреслення. **Дефіси не можна.**
Ім'я правила не перейменовується — тільки видалити і створити нове.

**Next**

### 2.3 Configure SQL statement

**SQL version:** `2016-03-23`

**SQL statement:**

```sql
SELECT *,
       timestamp() AS received_at,
       clientid()  AS client_id,
       topic(2)    AS student
FROM 'iot-course/demo/telemetry'
```

Розбір по частинах:

| Частина | Що робить |
|---|---|
| `SELECT *` | взяти весь payload як є |
| `timestamp() AS received_at` | час прильоту в AWS, **мілісекунди** |
| `clientid() AS client_id` | Client ID з TLS-сесії, підробити неможливо |
| `topic(2) AS student` | другий рівень топіка → `demo` |
| `FROM '...'` | з якого топіка ловимо |

Два місця, де помиляються найчастіше:
- **Одинарні лапки** навколо топіка. Не подвійні
- **`AS`** обовʼязковий. Без нього колонка називатиметься `timestamp()`, з дужками

Нумерація рівнів топіка починається з одиниці:
`iot-course` = 1, `demo` = 2, `telemetry` = 3.

**Next**

### 2.4 Attach rule actions

1. **Rule actions** → з дропдауна обрати **`DynamoDBv2`**

   У списку є дві схожі дії:
   - `DynamoDB` — кладе весь payload одним JSON-рядком в одну колонку
   - `DynamoDBv2` — розкладає кожне поле у свою колонку **← беремо цю**

2. **Table name** → з дропдауна обрати `iot_telemetry`

   Більше ця дія нічого не питає — ні ключів, ні полів. Бо `dynamoDBv2` бере ключі
   **прямо з результату SQL**. Вона шукає в даних поле з іменем `device_id`
   (partition key) і поле `received_at` (sort key).

   - `device_id` прийшов з прошивки
   - `received_at` дописало правило через `timestamp() AS received_at`

   Немає в даних поля з іменем ключа — запис не пройде.

3. **IAM role** → **Create new role**
   - Role name: `iot_rule_ddb_role`
   - **Create**

   Правило само по собі не має жодних прав. Роль дозволяє йому виконувати
   `dynamodb:PutItem` саме в цю таблицю і більше нічого. Це принцип найменших
   привілеїв — той самий, що в Policy для пристрою на Занятті 9.

**Next**

### 2.5 Review and create

Перевірити зведення → **Create**.

### 2.6 Перевірити, що правило увімкнене

**Rules** → `StoreTelemetry` → статус має бути **Enabled**.

Правило можна вимкнути, не видаляючи — зручно під час дебагу, коли не хочеться
засмічувати таблицю.

---

## Крок 3. Прошивка

### Опис main.cpp

**Wi-Fi — `connectWifi()`.** Без змін із Заняття 8. Канал 6 (`WiFi.begin(..., 6)`)
пропускає сканування — економить ~4 секунди в Wokwi. Повертає `false` при таймауті `WIFI_TIMEOUT`.

**NTP-синхронізація часу — `syncTime()`** (Заняття 10, слайд 18):

```cpp
configTime(0, 0, "pool.ntp.org");  // зсув 0, DST 0 — для TLS достатньо
```

**Без цього TLS впаде**, навіть із правильними сертифікатами: ESP32 стартує з 1970
року, і handshake вважає сертифікат AWS «ще не дійсним» (1970 < дата видачі).
Час треба синхронізувати **до** `connect()`.

**Підключення до AWS — `connectAWS()`.** Порядок кроків критичний:

```cpp
connectWifi();                          // 1. Wi-Fi
syncTime();                             // 2. час (до сертифікатів!)
net.setCACert(AWS_CERT_CA);             // 3. три файли зі слайда 10
net.setCertificate(AWS_CERT_CRT);
net.setPrivateKey(AWS_CERT_PRIVATE);
mqttClient.setServer(AWS_IOT_ENDPOINT, 8883);
mqttClient.setBufferSize(512);          // 256 замало — мовчки обрізає JSON
```

`WiFiClientSecure net` замість `WiFiClient` — єдина зміна на рівні транспорту
(слайд 17). `PubSubClient` той самий, що й у Занятті 8.

**MQTT Connect — `connectMQTT()`:**

```cpp
mqttClient.connect(THINGNAME);  // Client ID = THINGNAME (слайд 16)
```

Коди `mqttClient.state()` при невдачі:
- `-2` — помилка TLS/handshake → перевір **час** і **endpoint**
- `5` — відмовлено в доступі → перевір **AWS Policy**

**Неблокуючий таймер і reconnect.** `mqttClient.loop()` викликається лише коли
підключено — без нього брокер не отримує PING і відключає клієнта. Reconnect —
раз на 5 секунд без `delay()`.

### Публікація даних — оновлено для Заняття 11

Змінюється тільки `publishData()`. Було два поля, стало чотири:
`device_id` — партиційний ключ таблиці, дає пристрій;
`timestamp` — час вимірювання, `0`, якщо NTP не пройшов.
`received_at` тут НЕМА — його дописує правило в хмарі.

```cpp
void publishData(float temperature, float humidity) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Не підключено — пропускаємо");
        return;
    }

    time_t now;
    time(&now);

    // Буфер: payload підріс з двох полів до чотирьох.
    // snprintf обріже по межі й не впаде — але JSON прилетить
    // битий, і правило його не розбере (Заняття 4)
    char payload[160];
    snprintf(payload, sizeof(payload),
        "{\"device_id\":\"%s\",\"timestamp\":%lu,"
        "\"temperature\":%.1f,\"humidity\":%.1f}",
        THINGNAME, (unsigned long)now, temperature, humidity);

    Serial.print("[MQTT] Публікуємо: ");
    Serial.println(payload);

    bool ok = mqttClient.publish(TOPIC_TELEMETRY, payload);
    Serial.println(ok ? "[MQTT] OK" : "[MQTT] Помилка публікації");
}
```

**Буфер зріс з 80 до 160 байт.** Це не косметика. `snprintf` при нестачі місця
не падає — він чесно обріже рядок по межі буфера і поставить нуль-термінатор.
Ви отримаєте валідний C-рядок і невалідний JSON, без закриваючої дужки.
Пристрій його опублікує, `publish()` поверне `true`, у Serial буде "OK" —
а правило не розпарсить і в базу нічого не поїде. Мовчки.

**`THINGNAME`** береться з `secrets.h` — той самий, що Client ID і що ім'я Thing.
Інакше Policy не пустить підключення.

---

## Як запустити

1. Скопіювати `secrets.example.h` → `secrets.h` і заповнити (див. вище).
2. Відкрити проєкт у VS Code з розширенням **PlatformIO**.
3. Для симуляції — розширення **Wokwi for VS Code**, `F1 → Wokwi: Start Simulator`.
4. Відкрити **Serial Monitor** (швидкість `115200`).
5. Кожні 10 с у Serial — рядок `[MQTT] Публікуємо: ...` з підтвердженням.

---

## Крок 4. Перевірка — діагностична драбина

Перевіряти строго по сходинках, не перестрибуючи. Кожна відповідає на своє питання.

### Сходинка 1 — Serial Monitor (Wokwi)

Шукаємо рядок:

```
[MQTT] Публікуємо: {"device_id":"esp32lecture10","timestamp":1784301652,"temperature":27.0,"humidity":55.0}
```

Перевіряємо:
- JSON цілий, закриваюча дужка на місці
- `device_id` не порожній
- `timestamp` — схоже на реальну епоху, чи там `0`

`temperature` завжди ціле число (`.0`) і `humidity` завжди `55.0` — у цій прошивці
дані фейкові (`random(15, 40)` і константа), сенсор DHT22 з `diagram.json` наразі
не читається. Реальні дробові значення зʼявляться, коли підключите справжній зчитувач.

`timestamp: 0` означає, що NTP не пройшов і пристрій вважає, що зараз 1970 рік.
У Wokwi це буває — UDP на порту 123 працює нестабільно. **Демо все одно спрацює**,
бо ключем у таблиці є не цей timestamp.

**Відповідає на питання:** пристрій сформував payload?

### Сходинка 2 — MQTT test client

1. **AWS IoT Core** → ліве меню → **Test** → **MQTT test client**
2. Вкладка **Subscribe to a topic**
3. Topic filter: `iot-course/demo/telemetry`
4. **Subscribe**

Мають падати повідомлення з чотирма полями.

Саме через **Test → MQTT test client**, а не через сторінку Thing → Activity —
там підписка тільки на службові топіки `$aws/things/.../shadow`.

Тут ви бачите **чотири** поля — ті, що надіслав пристрій. `received_at` тут немає
і не буде: це те, що бачить брокер, а правило працює після нього.

**Відповідає на питання:** долетіло до AWS? TLS живий? Policy пускає?

### Сходинка 3 — Таблиця

1. **DynamoDB** → **Tables** → `iot_telemetry`
2. Кнопка **Explore table items**
3. **Run**

Має бути:

| device_id | received_at | client_id | student | temperature | humidity | timestamp |
|---|---|---|---|---|---|---|
| esp32lecture10 | 1784301655852 | esp32lecture10 | demo | 27.0 | 55.0 | 1784301652 |

Пристрій відправив **чотири** поля. У базі лежить **сім**. Три дописала хмара —
пристрій про них не знає і не витратив на них ні байта трафіку.

Зверніть увагу на `received_at` і `timestamp` поруч: 14 цифр і 10 цифр.
**`received_at` — мілісекунди. `timestamp` — секунди.** Різниця в тисячу разів.
Це знадобиться на Занятті 13, коли Grafana питатиме, у якій колонці час.

`device_id` та `client_id` зараз однакові — і так має бути завжди. Ліве — те, що
пристрій про себе розповів (просто рядок, який можна написати будь-який).
Праве — те, ким він виявився при вході, з TLS-сесії. Не збіглися — хтось
прикидається чужим пристроєм.

**Відповідає на питання:** правило спрацювало?

---

## Крок 5. Error Action — щоб AWS перестав мовчати

Зараз наше правило працює. Але якщо дія впаде — ви про це не дізнаєтесь **ніяк**.

Уточнення, яке важливо зрозуміти правильно. Мова не про випадок, коли повідомлення
не підійшло під `WHERE` — це нормальна робота фільтра. Мова про випадок, коли
повідомлення підійшло, правило спрацювало, полізло писати в базу — **і не змогло**.

Що при цьому відбувається:

| Хто | Що бачить |
|---|---|
| ESP32 | `publish()` повернув `true`, у Serial "OK" |
| MQTT test client | повідомлення прилетіло, красиве |
| Таблиця | порожньо |
| Помилки | **їх немає ніде** |

Повідомлення просто викидається. AWS зробить кілька спроб при тимчасових збоях,
і якщо всі провалились — дані пішли в небуття без жодного сліду.

**Error Action — це чорний ящик правила.** Дія впала → AWS формує окреме
повідомлення про збій і кладе туди, куди ви скажете.

### 5.1 Додати Error Action до правила

1. **AWS IoT Core** → **Manage** → **Message routing** → **Rules**
2. Обрати `StoreTelemetry` → **Edit**
3. Дійти до секції **Error action - optional**
4. **Add error action** → з дропдауна обрати **CloudWatch Logs**

Заповнити:

| Поле | Значення |
|---|---|
| Log group | **Create new** → `/aws/iot/rules/errors` |
| IAM role | **Create new role** → `iot_rule_cw_role` |

5. **Update**

Error Action **один на все правило**, скільки б у нього не було дій. Якщо впали
дві дії одразу — прилетить одне повідомлення, у якому будуть обидві помилки.

### 5.2 Що всередині повідомлення про помилку

```json
{
  "ruleName": "StoreTelemetry",
  "topic": "iot-course/demo/telemetry",
  "cloudwatchTraceId": "...",
  "clientId": "esp32lecture10",
  "base64OriginalPayload": "eyJkZXZpY2VfaWQiOiJlc3AzMmxlY3R1cmUxMCIsLi4u",
  "failures": [
    {
      "failedAction": "DynamoActionV2",
      "failedResource": "iot_telemetry",
      "errorMessage": "..."
    }
  ]
}
```

Розбір по полях:

| Поле | Навіщо |
|---|---|
| `ruleName` | яке саме правило впало (їх у вас буде багато) |
| `topic` | на який топік прилетіло повідомлення |
| `clientId` | який пристрій це надіслав |
| `failures[].failedAction` | яка саме дія не спрацювала |
| `failures[].errorMessage` | **чому** — конкретна причина, а не "щось не так" |
| `base64OriginalPayload` | **саме те повідомлення, яке ви втратили** |

Останнє поле — найцінніше. Це не лог "у вас проблема". Це сам загублений payload,
який можна розкодувати з base64 і подивитись, що саме пристрій відправив у той момент.

### 5.3 Перевірити, що воно працює — зламавши правило навмисно

Найкращий спосіб переконатись, що чорний ящик пише — це впасти.

1. **Rules** → `StoreTelemetry` → **Edit** → **SQL statement**
2. Прибрати рядок `timestamp() AS received_at,`:

```sql
SELECT *,
       clientid() AS client_id,
       topic(2)   AS student
FROM 'iot-course/demo/telemetry'
```

Тепер у результаті SQL немає поля `received_at` — а це sort key таблиці.
`dynamoDBv2` не зможе зібрати ключ і дія впаде.

3. **Update**. Пристрій у Wokwi лишається працювати
4. Подивитись **Serial** — там `[MQTT] OK`, пристрій щасливий
5. Подивитись **MQTT test client** — дані летять
6. Подивитись **таблицю** → **Run** — нових записів немає
7. **CloudWatch** → **Log groups** → `/aws/iot/rules/errors` → відкрити log stream

Там лежить повідомлення про помилку з конкретною причиною.

8. Повернути `timestamp() AS received_at,` назад → **Update** → дані знову йдуть

### 5.4 Правило життя

Кожне правило в проді має Error Action. Без винятків.

Це три хвилини налаштування один раз проти годин вгадування щоразу.

---

## Troubleshooting

| Симптом | Куди дивитись |
|---|---|
| Serial мовчить, реконект по колу | Це Заняття 10: сертифікати, endpoint, NTP |
| `mqttClient.state()` = -2 | TLS/handshake: перевірити час і endpoint |
| `mqttClient.state()` = 5 | Відмовлено: перевірити Policy та Client ID |
| Serial "OK", MQTT test client порожній | Policy, Client ID, регіон |
| MQTT test client OK, **таблиця порожня** | Див. нижче |

### "MQTT OK, таблиця порожня" — перевіряти в цьому порядку

1. **Регіон.** Правило і таблиця в різних регіонах. Найчастіша причина
2. **Топік у SQL** не збігається з `TOPIC_TELEMETRY` побуквенно
3. **Забули `AS received_at`** → `dynamoDBv2` не знайшов sort key
4. **Тип sort key** — String замість Number
5. **Правило вимкнене** (статус не Enabled)
6. **IAM-роль** створена не через "Create new role" і не має `PutItem`

**Але спочатку — не вгадуйте. Подивіться в CloudWatch:**
**Log groups** → `/aws/iot/rules/errors`

Якщо Error Action налаштований (Крок 5) — причина буде написана там прямим текстом,
разом із втраченим payload. Список вище потрібен тільки тоді, коли логів немає:
або Error Action не налаштований, або повідомлення взагалі не дійшло до правила
(тоді дивіться сходинку 2 драбини).

**Порожній лог помилок при порожній таблиці** — це окремий діагноз. Він означає,
що правило **не спрацювало взагалі**: не той топік у `FROM`, не той регіон,
або правило вимкнене. Дія не падала, бо її ніхто не викликав.

---


---


## Домашнє завдання

### Частина 1. Підключення до AWS IoT Core

- Свій Thing, свої сертифікати, своя Policy (тільки власні топіки)
- ESP32 публікує температуру + вологість на `iot-course/<ваше_ім'я>/telemetry` через TLS
- Раз на 30 секунд. `device_id` та `timestamp` — у payload
- Обробити: NTP не пройшов, MQTT відвалився, сенсор повернув помилку

### Частина 2. Rules Engine + DynamoDB

- Таблиця `iot_telemetry`: `device_id` (String) + `received_at` (Number)
- Правило: телеметрія → `dynamoDBv2`. Дописати `received_at`, `client_id`
- Error Action → CloudWatch Logs. Обов'язково

### Частина 3. Друге правило — алерт на перегрів

- Окреме правило: `WHERE temperature > 28` → дія `cloudwatchLogs`
- Це не Error Action. Це бізнес-логіка. Різні речі, спільний CloudWatch
- Симулювати перегрів

README: архітектурна схема, налаштування Thing/Policy/Rule, скріншоти.

---
