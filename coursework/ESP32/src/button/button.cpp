#include "button.h"

// ISR виставляє прапорець миттєво, незалежно від того, що зараз робить
// loop() (навіть якщо він застряг у блокуючому Wi-Fi/NTP/TLS виклику) —
// на відміну від digitalRead() в loop(), переривання не залежить від
// того, чи "співпало" опитування з моментом натискання.
static volatile bool pressFlag = false;
static volatile unsigned long lastIsrMillis = 0;

static void IRAM_ATTR onButtonFalling() {
    unsigned long now = millis();
    // Дебаунс прямо в ISR: ігноруємо повторні спрацювання (дребезг
    // контактів) протягом DEBOUNCE_DELAY після попереднього прийнятого
    if (now - lastIsrMillis > DEBOUNCE_DELAY) {
        lastIsrMillis = now;
        pressFlag = true;
    }
}

void button_begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonFalling, FALLING);
}

bool button_pressed() {
    if (!pressFlag) return false;
    pressFlag = false;   // скидаємо ДО обробки — як mqtt_take_command()
    return true;
}
