#pragma once
#include <Arduino.h>
#include "../secrets.h"

// ═══════════════════════════════════════════════════════════
// DEVICE SHADOW — стан, який чекає на нас у хмарі.
//
// Різниця з командою: команда — це подія, яка летить у конкретну мить.
// Немає кому слухати (пристрій спить) — події більше немає. Shadow —
// це документ, який лежить в AWS завжди. Пристрій сам приходить і
// питає «як має бути», коли йому зручно (кожне пробудження).
//
// Хмара (FastAPI) пише  desired  — як має бути.
// Пристрій пише         reported — як є насправді.
// AWS сам рахує різницю і віддає нам ТІЛЬКИ її.
// ═══════════════════════════════════════════════════════════

// ═══ ЗАРЕЗЕРВОВАНІ ТОПІКИ ═══
// Ті самі $aws, що й у jobs — ми їх не вигадуємо.
// Це «класична» (безіменна) тінь: префікс без /name/<shadowName>.
#define TOPIC_SHADOW_GET       "$aws/things/" THINGNAME "/shadow/get"
#define TOPIC_SHADOW_GET_ACC   "$aws/things/" THINGNAME "/shadow/get/accepted"
#define TOPIC_SHADOW_UPDATE    "$aws/things/" THINGNAME "/shadow/update"
#define TOPIC_SHADOW_DELTA     "$aws/things/" THINGNAME "/shadow/update/delta"

// Відповідь на НАШ запис. Без цих двох звіт — постріл у темряву:
// AWS мовчки не прийме документ, а ми вже поїхали спати.
//   accepted → reported оновлено, дельта зникне
//   rejected → у тілі код і причина (403 Forbidden = Policy)
#define TOPIC_SHADOW_UPD_ACC   "$aws/things/" THINGNAME "/shadow/update/accepted"
#define TOPIC_SHADOW_UPD_REJ   "$aws/things/" THINGNAME "/shadow/update/rejected"

// ═══════════════════════════════════════════════════════════
// РІЗНИЦЯ, ЯКУ ТРЕБА ЗАСТОСУВАТИ.
// Прапорці has* важливі: у дельті приходять НЕ ВСІ поля, а тільки
// ті, що розійшлись. Якщо hasInterval == false — інтервал збігається,
// його чіпати не можна.
// ═══════════════════════════════════════════════════════════
typedef struct {
    bool hasIndicator;
    char indicator[8];   // "on" / "off" — бажаний стан LED

    bool hasInterval;
    int  interval;       // секунди сну
} ShadowDelta;

// Викликати з callback у mqtt.cpp.
// Повертає true, якщо топік був shadow-івський і повідомлення забрано.
bool shadow_on_message(char* topic, byte* payload, unsigned int length);

// Питаємо повний документ. Відповідь прийде в get/accepted — НЕ
// синхронно. Після виклику треба чекати через shadow_wait_for_delta().
void shadow_request();

// Крутить mqtt_poll(), поки не приїде відповідь або не вийде час.
// НЕ delay() — той не читає сокет.
// Повертає false і при таймауті, і коли AWS промовчав, бо різниці немає.
// Тиша — це нормальна відповідь «стан уже правильний», а не помилка.
bool shadow_wait_for_delta(unsigned long timeoutMs);

// Забрати різницю, якщо вона прийшла. Патерн той самий, що
// mqtt_take_command() / mqtt_take_job().
bool shadow_take_delta(ShadowDelta* out);

// Звітуємо фактичний стан і ЧЕКАЄМО підтвердження від AWS.
// Поки reported не оновлено — хмара вважає, що ми бажане не виконали,
// і слатиме ту саму дельту знову.
// Повертає true тільки якщо приїхав update/accepted.
bool shadow_report(const char* indicator, int interval);
