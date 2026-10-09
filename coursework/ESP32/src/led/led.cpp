#include "led.h"
#include "led_topic.h"
#include "../mqtt/mqtt.h"

void led_begin() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
}

void led_handle_command(const char* cmd) {
    Serial.print("[CMD] Отримано: ");
    Serial.println(cmd);

    // Шукаємо саме значення в лапках, а не пару "ключ":"значення" —
    // тоді пробіли після двокрапки не мають значення.
    // "on" не збігається всередині "off": лапки роблять токени різними
    if (strstr(cmd, "\"on\"") != NULL) {
        digitalWrite(LED_PIN, HIGH);
        Serial.println("[CMD] LED увімкнено");
        mqtt_publish(TOPIC_EVENTS, "{\"event\":\"led_changed\",\"value\":\"on\"}");

    } else if (strstr(cmd, "\"off\"") != NULL) {
        digitalWrite(LED_PIN, LOW);
        Serial.println("[CMD] LED вимкнено");
        mqtt_publish(TOPIC_EVENTS, "{\"event\":\"led_changed\",\"value\":\"off\"}");

    } else {
        Serial.println("[CMD] Невідома команда — ігноруємо");
    }
}
