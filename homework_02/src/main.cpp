#include <Arduino.h>
#include <esp_system.h>

// Пункт 1: структура даних сенсора
struct SensorData {
  float temperature;   // 15.0 - 30.0 C
  float humidity;      // 30.0 - 65.0 %
  unsigned long timestamp; // мс від старту програми (реальний час роботи, не випадковий)
};

const unsigned long SENSOR_INTERVAL_MS = 20000; // 20 секунд
const unsigned long HEAP_INTERVAL_MS   = 60000; // 1 хвилина

unsigned long lastSensorPrint = 0;
unsigned long lastHeapPrint   = 0;

// Пункт 2: генерація значень сенсора
SensorData readSensor() {
  SensorData data;
  data.temperature = random(150, 301) / 10.0f;  // 15.0 .. 30.0
  data.humidity    = random(300, 651) / 10.0f;  // 30.0 .. 65.0
  data.timestamp   = millis();
  return data;
}

void printSensorData(const SensorData &data) {
  Serial.printf(
    "[t=%lus] Temperature: %.1f C | Humidity: %.1f %%\r\n",
    data.timestamp / 1000UL,
    data.temperature,
    data.humidity
  );
}

// Пункт 4: моніторинг вільної пам'яті
void printFreeHeap() {
  static uint32_t prevHeap = 0;
  static bool hasPrev = false;

  uint32_t heap = ESP.getFreeHeap();

  if (hasPrev) {
    int32_t diff = (int32_t)heap - (int32_t)prevHeap;
    Serial.printf("[t=%lus] Free heap: %u bytes (%+d since last)\r\n", millis() / 1000UL, heap, diff);
  } else {
    Serial.printf("[t=%lus] Free heap: %u bytes\r\n", millis() / 1000UL, heap);
    hasPrev = true;
  }

  prevHeap = heap;
}

// Пункт 5: демонстрація переповнення uint8_t (аналіз у NOTES.md)
void overflowDemo() {
  uint8_t a = 200;
  uint8_t b = 100;
  uint8_t sum = a + b;
  Serial.printf("Overflow demo: a=%u, b=%u, a+b=%u (wraps around 256)\r\n", a, b, sum);
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  overflowDemo();
  printFreeHeap();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensorPrint >= SENSOR_INTERVAL_MS) {
    lastSensorPrint = now;
    SensorData data = readSensor();
    printSensorData(data);
  }

  if (now - lastHeapPrint >= HEAP_INTERVAL_MS) {
    lastHeapPrint = now;
    printFreeHeap();
  }
}
