#include "net.h"
#include <WiFi.h>
#include "esp_sntp.h"
#include "../secrets.h"

#define WIFI_TIMEOUT 10000  // максимум 10 секунд на підключення
#define NTP_TIMEOUT  15000  // мс

// Як часто SNTP сам перезвіряє годинник з сервером (за замовчуванням — 1 год).
// У Wokwi віртуальний годинник ESP32 зупиняється, коли симуляція на паузі
// (Mac заснув, вкладка/панель неактивна), і після відновлення timestamp
// "застрягає" в минулому аж до наступної синхронізації
#define NTP_RESYNC_INTERVAL (5 * 60 * 1000UL)

bool net_wifi_connected() {
    return WiFi.status() == WL_CONNECTED;
}

bool net_wifi_connect() {
    Serial.print("[Wi-Fi] Підключаємось");
    // Канал 6 — пропускає сканування, економить ~4 секунди в Wokwi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

    unsigned long start = millis();
    while (!net_wifi_connected()) {
        if (millis() - start > WIFI_TIMEOUT) {
            Serial.println(" таймаут!");
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println(" OK");
    Serial.print("[Wi-Fi] IP: ");
    Serial.println(WiFi.localIP());

    // DHCP у Wokwi іноді не віддає робочий DNS-сервер — тоді hostByName()
    // падає з "DNS Failed" ще до TLS, навіть коли Wi-Fi і IP вже є.
    // Прописуємо публічний DNS явно, лишаючи вже отримані IP/gateway/subnet.
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));

    return true;
}

bool net_time_sync() {
    Serial.print("[NTP] Синхронізація часу");
    sntp_set_sync_interval(NTP_RESYNC_INTERVAL);
    configTime(0, 0, "pool.ntp.org");  // зсув 0, DST 0 — для TLS достатньо

    struct tm timeinfo;
    unsigned long start = millis();
    while (!getLocalTime(&timeinfo)) {
        if (millis() - start > NTP_TIMEOUT) {
            Serial.println(" таймаут!");
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println(" OK");
    return true;
}
