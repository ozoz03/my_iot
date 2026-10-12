#include "dht.h"
#include <DHT.h>

static DHT dht(DHT_PIN, DHT22);

void dht_begin() {
    dht.begin();
}

bool dht_read(float &temperature, float &humidity) {
    temperature = dht.readTemperature();
    humidity    = dht.readHumidity();
    return !isnan(temperature) && !isnan(humidity);
}
