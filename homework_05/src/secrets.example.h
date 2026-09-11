#ifndef SECRETS_H
#define SECRETS_H

// Скопіюй цей файл у secrets.h і заповни своїми значеннями.
// secrets.h у .gitignore — НІКОЛИ не комітити приватний ключ у git!

// ═══════════════════════════════════════════════════════════
// WI-FI
// ═══════════════════════════════════════════════════════════
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// ═══════════════════════════════════════════════════════════
// AWS IOT CORE
// ═══════════════════════════════════════════════════════════
// Client ID МАЄ дорівнювати імені Thing — інакше Policy заблокує (слайд 16)
#define THINGNAME        "esp32lecture10"
// Твій endpoint: AWS IoT Console → Settings → Device data endpoint
#define AWS_IOT_ENDPOINT "xxxxxxxxxxxxx-ats.iot.eu-north-1.amazonaws.com"

// ═══════════════════════════════════════════════════════════
// СЕРТИФІКАТИ — 3 файли зі слайда 10
// Вставити вміст .pem файлів, завантажених у Занятті 9, як є
// ═══════════════════════════════════════════════════════════

// AmazonRootCA1.pem — перевірка сервера ("це справді AWS?")
static const char AWS_CERT_CA[] = R"EOF(
-----BEGIN CERTIFICATE-----
...вставити вміст AmazonRootCA1.pem...
-----END CERTIFICATE-----
)EOF";

// certificate.pem.crt — паспорт пристрою ("ось хто я")
static const char AWS_CERT_CRT[] = R"EOF(
-----BEGIN CERTIFICATE-----
...вставити вміст certificate.pem.crt...
-----END CERTIFICATE-----
)EOF";

// private.pem.key — секретний доказ ("паспорт справді мій")
// НІКОЛИ не комітити цей файл у git!
static const char AWS_CERT_PRIVATE[] = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
...вставити вміст private.pem.key...
-----END RSA PRIVATE KEY-----
)EOF";

#endif
