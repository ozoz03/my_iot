# Домашня робота 6 — FastAPI: бекенд, що не тільки читає, а й командує

Три частини, що працюють разом: пристрій публікує телеметрію в хмару,
бекенд її читає й показує, і той самий бекенд може послати пристрою команду
назад. Деталі кожної частини — у власному README (`ESP32/README.md`,
`FastAPI/README.md`, `HTML/README.md`); тут — загальна картина, список
ресурсів AWS і те, що ще не зроблено.

---

## Архітектура (обидва напрямки)

```
                 ВГОРУ: телеметрія                      ВНИЗ: команда
┌──────────────┐  MQTT/TLS                    ┌──────────────┐  fetch POST
│  ESP32       │  topic:                      │  Браузер     │  {"value":"on"}
│  (DHT22+LDR) │  iot-course/ozasymenko/       │  index.html  │
└──────┬───────┘  sensors/data                 └──────┬───────┘
       │ publish                                      │ POST /actuators/led
       ▼                                               ▼
┌──────────────────────────────┐              ┌───────────────────┐
│  AWS IoT Core (eu-north-1)   │              │  FastAPI :8000    │
│  Rules Engine → DynamoDB     │              └─────────┬─────────┘
└──────┬────────────────────────┘                        │ boto3
       │ dynamoDBv2 (роль iot_write_db)                   │ iot-data.publish
       ▼                                                  ▼
┌──────────────┐                              ┌──────────────────────────┐
│  DynamoDB    │ ◄──── boto3 query ───────────│  AWS IoT Core             │
│ iot_telemetry│       GET /sensors/latest     │  topic:                  │
└──────────────┘       GET /sensors/history    │  iot-course/ozasymenko/  │
                                                │  commands/led            │
                                                └──────────┬───────────────┘
                                                           │ subscribe
                                                           ▼
                                                    ┌──────────────┐
                                                    │  ESP32 → LED │
                                                    └──────┬───────┘
                                                           │ publish ack
                                                           │ topic: .../commands/led/ack
                                                           ▼
                                                    AWS IoT Core (назад)
```

Два незалежні канали: «вгору» (телеметрія: пристрій → Rule → DynamoDB →
FastAPI → браузер) і «вниз» (команда: браузер → FastAPI → AWS IoT →
пристрій). Жодна частина не знає адреси іншої — спільна точка це назва
топіка/таблиці, а не IP чи URL.

Третій, окремий споживач «вгору»-каналу — **Grafana**: вона не читає
DynamoDB/AWS напряму, а б'є в ті самі `GET /sensors/latest` і
`GET /sensors/history` FastAPI, що й міг би будь-який інший клієнт. Деталі —
нижче.

---

## Структура репозиторію

```
ESP32/     — прошивка (PlatformIO, модулі: net, mqtt, dht, ldr, led, button)
FastAPI/   — бекенд: читання DynamoDB + публікація команд в AWS IoT
HTML/      — одна сторінка з двома кнопками (Увімкнути / Вимкнути)
grafana/   — dashboard.json для імпорту в Grafana
```

---

## Ресурси AWS

| Ресурс | Назва/ARN | Де використовується |
|---|---|---|
| Thing | `esp32-zasymenko` | ESP32, сертифікат X.509 |
| IoT Policy (на сертифікаті) | `Connect` (по `${iot:Connection.Thing.ThingName}`) + `Publish` на `topic/iot-course/ozasymenko/*` + `Subscribe`/`Receive` на `.../commands/*` | підключення, телеметрія і прийом команд — деталі в `ESP32/README.md` |
| Rules Engine | `StoreTelemetry` (→ DynamoDB), `TemperatureMoreThan28` (→ CloudWatch) | записує телеметрію, лог температурних подій |
| IAM-роль для Rule | `iot_write_db` | `dynamodb:PutItem` + запис помилок у CloudWatch Logs |
| DynamoDB таблиця | `iot_telemetry` (pk `device_id`, sk `received_at`) | зберігає телеметрію |
| CloudWatch Log Groups | `iot_db_store_errors`, `temperatureLogGroup` | помилки Rule / температурні події |
| IAM-користувач (FastAPI, `.env`) | `dynamodb:Query` на `table/iot_telemetry` + `iot:Publish` на `topic/iot-course/ozasymenko/commands/*` | читання телеметрії й публікація команд із бекенда |
| MQTT топік команд | `iot-course/ozasymenko/commands/led` | FastAPI публікує, ESP32 підписується |
| MQTT топік ack | `iot-course/ozasymenko/commands/led/ack` | ESP32 публікує після виконання команди, підтверджуючи результат |

---

## Як запустити

### ESP32 (PlatformIO / Wokwi)

```bash
cd ESP32
cp src/secrets.example.h src/secrets.h   # заповнити своїми значеннями
# залити сертифікати, THINGNAME, AWS_IOT_ENDPOINT
pio run                                   # або Wokwi: Run
```

### FastAPI (:8000)

```bash
cd FastAPI
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env                      # заповнити AWS-ключами й регіоном
uvicorn main:app --reload
```

Swagger UI: http://127.0.0.1:8000/docs

### HTML

Відкрити `HTML/index.html` подвійним кліком — бʼє в `http://localhost:8000`.

### Grafana

![Grafana dashboard](ESP32/images/grafana.png)

[Демонстрація роботи (запис екрана)](<ESP32/images/Screen Recording 2026-10-03 at 23.08.35.mov>)

Дашборд читає JSON напряму з FastAPI — без окремого проміжного API чи БД
на боці Grafana.

1. Встановити плагін **Infinity** (`yesoreyeram-infinity-datasource`):
   `grafana-cli plugins install yesoreyeram-infinity-datasource` (або через
   UI: Connections → Add new connection → Infinity), і додати джерело даних
   цього типу (URL можна лишити порожнім — він заданий у кожному панелі
   окремо).
2. **FastAPI має вже бути запущений на :8000** — Grafana ходить за живими
   даними в момент рендеру панелі, нічого не кешує сама.
3. Dashboards → Import → завантажити `grafana/dashboard.json`.

Чотири панелі, усі на `GET /sensors/latest` (три) і
`GET /sensors/history?minutes=30` (одна):

| Панель | Тип | Джерело |
|---|---|---|
| Час отримання | stat | `received_at` з `/sensors/latest` |
| Вологість | gauge | `humidity` з `/sensors/latest` |
| Час відправки | stat | `timestamp` з `/sensors/latest` |
| Температура | timeseries | `received_at`+`temperature` з `/sensors/history?minutes=30` |

Автооновлення — `refresh: 30s`, часове вікно — `now-30m` до `now`.

---

## Підтвердження виконання команди

ESP32 (`led.cpp`) після виконання команди публікує ack назад у хмару — на
`iot-course/ozasymenko/commands/led/ack`, окремий від топіка команд, яким
пристрій тільки слухає:

```json
{"value":"on","status":"ok"}
```

Доказ: AWS IoT Console → MQTT test client → підписатись на
`iot-course/ozasymenko/commands/led/ack`. Натиснути кнопку в браузері —
побачити в тест-клієнті спершу команду на `.../commands/led`, тоді ack на
`.../commands/led/ack`. `202 Accepted` від FastAPI підтверджує лише доставку
до брокера; ack у цьому топіку підтверджує, що світлодіод справді змінив
стан.
