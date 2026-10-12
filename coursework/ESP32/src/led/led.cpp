#include "led.h"
#include "led_topic.h"
#include "../mqtt/mqtt.h"

void led_begin() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
}

// Пряме керування без JSON — "on"/"off", без лапок. Після Deep Sleep
// сон знеструмлює GPIO, тож це значення треба відтворювати руками на
// початку кожного циклу (work_cycle() у main.cpp), інакше reported
// тіні бреше: написано "on", а лампочка фізично не горить.
void led_set(const char* state) {
    bool on = (strcmp(state, "on") == 0);

    digitalWrite(LED_PIN, on ? HIGH : LOW);

    Serial.print("[LED] ");
    Serial.println(on ? "увімкнено" : "вимкнено");
}

void led_handle_command(const char* cmd) {
    Serial.print("[CMD] Отримано: ");
    Serial.println(cmd);

    // Шукаємо саме значення в лапках, а не пару "ключ":"значення" —
    // тоді пробіли після двокрапки не мають значення.
    // "on" не збігається всередині "off": лапки роблять токени різними
    if (strstr(cmd, "\"on\"") != NULL) {
        led_set("on");
        mqtt_publish(TOPIC_EVENTS, "{\"event\":\"led_changed\",\"value\":\"on\"}");

    } else if (strstr(cmd, "\"off\"") != NULL) {
        led_set("off");
        mqtt_publish(TOPIC_EVENTS, "{\"event\":\"led_changed\",\"value\":\"off\"}");

    } else {
        Serial.println("[CMD] Невідома команда — ігноруємо");
    }
}
