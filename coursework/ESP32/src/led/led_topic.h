#pragma once

// Топік подій пристрою — спільний простір для різних типів подій
// ("led_changed" сьогодні, інші завтра), а не окремий ack-підтопік під
// командами. Тип події несе поле "event" у payload, а не назва топіка.
// Уже покритий тим самим Publish-дозволом у IoT Policy, що й телеметрія
// (wildcard "iot-course/ozasymenko/*").
#define TOPIC_EVENTS "iot-course/ozasymenko/events"
