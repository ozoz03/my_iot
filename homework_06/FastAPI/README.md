# Лекція 14 — FastAPI: бекенд, що не тільки читає, а й командує

Розвиток Заняття 12. Там бекенд лише **читав** телеметрію з DynamoDB — труба
з краном на кінці. Тепер додаємо зворотний напрямок: `POST /actuators/led`
публікує команду в AWS IoT Core, а ESP32 її отримує і вмикає світлодіод.
Кран став ще й вимикачем.

Ця тека — середня ланка з трьох: `HTML/` (кнопки в браузері) → **`FastAPI/`** →
`ESP32/` (прошивка з підпискою на команди).

---

## Архітектура

```
┌──────────────┐  HTML/index.html
│   Браузер    │  дві кнопки: Увімкнути / Вимкнути
└──────┬───────┘
       │ POST /actuators/led  {"value":"on"}   (fetch, CORS)
       ▼
┌───────────────────┐        GET  /health
│  FastAPI          │  ←──   GET  /sensors/latest
│  uvicorn :8000    │        GET  /sensors/history?minutes=30
└───┬───────────┬───┘        POST /actuators/led
    │           │
    │ boto3     │ boto3 iot-data.publish (QoS 1)
    │ query     │ topic: iot-course/demo/commands/led
    ▼           ▼
┌──────────┐  ┌──────────────────┐
│ DynamoDB │  │  AWS IoT Core    │
│iot_teleme│  └─────────┬────────┘
│   try    │            │ MQTT over TLS
└────▲─────┘            ▼
     │            ┌──────────┐
     │ Rules      │  ESP32   │ → LED
     │ Engine     └────┬─────┘
     └─────────────────┘  topic: iot-course/demo/telemetry
```

**Два незалежні канали.** «Вгору» — пристрій публікує телеметрію, правило кладе
її в таблицю, бекенд читає таблицю. «Вниз» — бекенд публікує команду в топік,
пристрій її забирає. Бекенд і пристрій так само не знають одне про одного:
спільна точка — назва топіка, не адреса.

---

## Параметри цього проєкту

| Що | Значення |
|---|---|
| Регіон | `eu-north-1` |
| Таблиця DynamoDB | `iot_telemetry` (створена на Занятті 11) |
| `device_id` | `esp32-zasymenko` |
| Топік команд | `iot-course/demo/commands/led` |
| Топік телеметрії | `iot-course/demo/telemetry` |
| Порт API | `8000` (uvicorn за замовчуванням) |

---

## Структура проєкту

```
main.py           — FastAPI-застосунок, ендпоінти, CORS, валідація команди
db.py             — читання DynamoDB (boto3 query)
iot_client.py     — публікація команди в AWS IoT Core (boto3 iot-data)
requirements.txt  — залежності Python
.env              — AWS-ключі та конфігурація (У .gitignore!)
.env.example      — шаблон .env для копіювання
```

Читання й запис розведені по різних модулях навмисно: це різні сервіси AWS
(`dynamodb` і `iot-data`), різні дозволи IAM і різні причини зламатися.

---

## Налаштування .env

`.env` містить AWS-ключі і **до git не потрапляє** (у `.gitignore`).

1. Скопіювати `.env.example` → `.env`.
2. Вписати свої значення:

| Змінна | Що це |
|---|---|
| `AWS_ACCESS_KEY_ID` | ключ IAM-користувача |
| `AWS_SECRET_ACCESS_KEY` | секретна частина ключа |
| `AWS_DEFAULT_REGION` | регіон — `eu-north-1` (звідси його бере і DynamoDB, і IoT) |
| `TABLE_NAME` | `iot_telemetry` |
| `DEVICE_ID` | `esp32-zasymenko` — partition key, за яким робимо query |

**Дозволи IAM.** До `dynamodb:Query` із Заняття 12 тепер додається
`iot:Publish` на ARN топіка команд:

```
arn:aws:iot:eu-north-1:<account-id>:topic/iot-course/demo/commands/led
```

Принцип найменших привілеїв той самий, що на Заняттях 9 і 11: два дозволи на
два конкретні ресурси. Не `AdministratorAccess` «щоб працювало».

> `load_dotenv()` у `main.py` викликається **до** `import db` — бо `db.py`
> читає змінні середовища прямо при імпорті. Поміняєте порядок — отримаєте
> підключення до регіону `None`.

---

## Як запустити

```powershell
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
uvicorn main:app --reload
```

Або через FastAPI CLI (той самий uvicorn з `--reload` під капотом):

```powershell
fastapi dev main.py
```

Відкрити http://127.0.0.1:8000/docs — Swagger UI, згенерований самим FastAPI.
Кожен ендпоінт викликається прямо звідти, включно з POST.

Далі відкрити `HTML/index.html` у браузері (просто подвійним кліком) — кнопки
б'ють у `http://localhost:8000/actuators/led`.

---

## Ендпоінти

| Метод | Шлях | Що робить |
|---|---|---|
| GET | `/health` | `{"status": "ok"}` — живий чи ні |
| GET | `/sensors/latest` | останній запис пристрою; `404`, якщо даних ще немає |
| GET | `/sensors/history?minutes=30` | записи за останні N хвилин |
| POST | `/actuators/led` | публікує команду; `202`, `422` або `502` |

### POST /actuators/led

```jsonc
// запит
{ "value": "on" }        // або "off"

// відповідь 202 Accepted
{ "accepted": true, "value": "on" }
```

**Чому 202, а не 200.** 200 означає «зроблено». Ми ж не знаємо, чи ESP32
взагалі ввімкнена — ми лише віддали команду брокеру. 202 Accepted чесно
каже: прийняв, передав далі, за результат не ручаюсь. Щоб дізнатися
результат — потрібен зворотний звіт від пристрою (див. «Копни глибше»).

**Валідація — це Pydantic, не `if`.** `Literal["on", "off"]` у моделі
`LedCommand` означає, що FastAPI відхилить будь-що інше сам, до входу в
функцію, з відповіддю `422` і поясненням, що саме не так. У тілі ендпоінта
жодної перевірки писати не треба.

**502 — це чесна помилка.** Якщо `boto3` не достукався до AWS IoT, винен не
клієнт, а зовнішній сервіс — тому 502 Bad Gateway, а не 500. Спробуйте
відключити інтернет і натиснути кнопку: побачите цей шлях.

### Публікація команди (iot_client.py)

```python
iot = boto3.client("iot-data")
iot.publish(topic=TOPIC_CMD, qos=1, payload=payload)
```

**`iot-data`, а не `iot`.** Клієнт `iot` — це керування (створити Thing,
політику, правило). Клієнт `iot-data` — це data plane, сама передача
повідомлень. Переплутати легко, помилка буде «немає методу publish».

**QoS 1 для команди.** Команда унікальна: якщо загубиться — світло не
ввімкнеться, і ніхто про це не дізнається. Телеметрія їде QoS 0, бо там
наступний пакет за 10 секунд перекриє втрачений.

**`separators=(",", ":")`** — JSON без пробілів. Дрібниця, але на пристрої
буфер `PubSubClient` фіксований (512 байт), і кожен зайвий байт — це байт
не на користь.

### Читання (db.py)

**`get_latest()`** — query у партицію пристрою, `ScanIndexForward=False`
(найновіші згори), `Limit=1`. DynamoDB читає рівно один item — швидко й дешево.

**`get_history(minutes)`** — query по діапазону sort key:
`device_id = ... AND received_at >= cutoff`. Межа рахується в **мілісекундах**
(`time.time() * 1000`), бо `received_at` записаний правилом через
`timestamp()` — а він у мілісекундах (Заняття 11).

**Чому query, а не scan:** query йде точно в партицію по ключу і читає тільки
потрібне. Scan перечитує всю таблицю і фільтрує потім. Ключ будувався саме під
ці запити ще на Занятті 11.

---

## CORS — чому без нього нічого не працює

`HTML/index.html` відкривається як `file://` (або з іншого порту), а API живе
на `localhost:8000`. Для браузера це **різні origin** — і він блокує відповідь
fetch, навіть якщо сервер її чесно віддав. У консолі буде «CORS policy», у
логах uvicorn — успішний `200`. Помилка не там, де здається.

```python
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],            # демо; у проді — список конкретних доменів
    allow_credentials=False,        # кукі не використовуємо
    allow_methods=["GET", "POST"],  # default — тільки GET, POST треба дописати
    allow_headers=["Content-Type"], # без нього preflight на JSON не пройде
)
```

Три пастки, кожна коштує пів години:

- **`allow_methods` за замовчуванням — тільки `GET`.** Наш POST без цього
  рядка не пройде preflight.
- **`Content-Type: application/json` робить запит «непростим»** — браузер
  спершу шле `OPTIONS` (preflight). Не дозволили заголовок — preflight
  провалився, самого POST навіть не буде.
- **`allow_origins=["*"]` разом із `allow_credentials=True` не працює** —
  специфікація забороняє. Тому тут `False`.

---

## Перевірка ланцюга

Ланцюг довгий, тож перевіряти його треба ланками, а не цілком:

1. **API живий** — `GET /health` у `/docs`.
2. **Команда доходить до AWS** — натиснути кнопку, дивитись відповідь `202`.
3. **Команда в топіку** — AWS IoT Console → MQTT test client → підписатись на
   `iot-course/demo/commands/led`. Побачили JSON — бекенд свою роботу зробив.
4. **Пристрій отримав** — Serial Monitor у Wokwi: `[CMD] Отримано: ...`.
5. **Світлодіод** — власне LED на D2 у симуляторі.

Обірвалось на кроці 3 — винен бекенд або IAM. Дійшло до 3, але не до 4 — винен
пристрій: підписка, Policy, TLS.

---

## Troubleshooting

| Симптом | Куди дивитись |
|---|---|
| У консолі браузера «CORS policy», у логах uvicorn `200` | `allow_methods` / `allow_headers` у `CORSMiddleware` |
| Кнопка → «Помилка мережі» | сервер не запущений або не той порт |
| POST → `422` | тіло не `{"value":"on"\|"off"}` — Pydantic відхилив |
| POST → `502` | немає доступу до AWS IoT: ключі, регіон, інтернет |
| POST → `202`, але LED мовчить | команда в топіку є — дивіться пристрій (крок 4 вище) |
| `AccessDeniedException` на publish | у IAM немає `iot:Publish` на ARN топіка |
| `Unable to locate credentials` | `.env` не скопійований або `load_dotenv()` після `import db` |
| `ResourceNotFoundException` | не той регіон або не та назва таблиці |
| `/sensors/latest` → 404 | таблиця порожня — запустіть пристрій |
| `/sensors/history` → `[]` | дані старіші за N хвилин — збільшіть `minutes` |

---

## Ключові факти заняття

- Бекенд тепер має **два напрямки**: читає DynamoDB і пише в IoT Core. Це
  різні сервіси, різні клієнти boto3, різні дозволи IAM
- `202 Accepted` ≠ `200 OK`. Ми відповідаємо за передачу команди, не за її виконання
- Валідацію робить Pydantic (`Literal`), а не `if` у тілі функції
- CORS — це захист **браузера**, не сервера. `curl` працює, кнопка ні — шукайте тут
- QoS 1 для команд, QoS 0 для телеметрії. Команду ніхто не повторить
- `iot-data` — передача повідомлень; `iot` — керування інфраструктурою
- **Пристрій дає дані — хмара дає гарантії — бекенд дає доступ і керування**

---

## Рекомендована література

### FastAPI

- Офіційна документація — [fastapi.tiangolo.com](https://fastapi.tiangolo.com/)
- Request Body (Pydantic-моделі) — [fastapi.tiangolo.com/tutorial/body](https://fastapi.tiangolo.com/tutorial/body/)
- CORS Middleware — [fastapi.tiangolo.com/tutorial/cors](https://fastapi.tiangolo.com/tutorial/cors/)
- Обробка помилок (`HTTPException`) — [fastapi.tiangolo.com/tutorial/handling-errors](https://fastapi.tiangolo.com/tutorial/handling-errors/)

### CORS і HTTP

- CORS — [developer.mozilla.org/…/CORS](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS)
- Preflight-запит — [developer.mozilla.org/…/Preflight_request](https://developer.mozilla.org/en-US/docs/Glossary/Preflight_request)
- `202 Accepted` — [developer.mozilla.org/…/Status/202](https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/202)

### boto3 / AWS

- `IoTDataPlane.publish` — [boto3.amazonaws.com/…/iot-data/client/publish](https://boto3.amazonaws.com/v1/documentation/api/latest/reference/services/iot-data/client/publish.html)
- `Table.query` — [boto3.amazonaws.com/…/dynamodb/table/query](https://boto3.amazonaws.com/v1/documentation/api/latest/reference/services/dynamodb/table/query.html)
- Політики AWS IoT (publish/subscribe) — [docs.aws.amazon.com/iot/…/iot-policies](https://docs.aws.amazon.com/iot/latest/developerguide/iot-policies.html)

### Копни глибше (по бажанню)

- **Зворотний звіт від пристрою** — топік `.../commands/led/ack`, щоб 202 колись перетворилось на реальний статус
- **Device Shadow** — бажаний і фактичний стан пристрою замість «випущеної в нікуди» команди
- **Pydantic response models** — типізована відповідь endpoint замість голого dict
- **def vs async def** — чому з синхронним boto3 беремо `def` (FastAPI винесе в threadpool)
- **Автентифікація ендпоінта** — зараз кнопку може натиснути будь-хто, хто знає адресу
- **Пагінація DynamoDB** (`LastEvaluatedKey`) — коли історія не влазить у 1 MB

---
