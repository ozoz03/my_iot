#pragma once

// Топік, на який ESP32 публікує підтвердження виконання LED-команди —
// окремий від топіка команд (mqtt.cpp), яким пристрій лише слухає.
// Вже покритий тим самим Publish-дозволом у IoT Policy, що й телеметрія
// (wildcard "iot-course/ozasymenko/*").
#define TOPIC_LED_ACK "iot-course/ozasymenko/commands/led/ack"
