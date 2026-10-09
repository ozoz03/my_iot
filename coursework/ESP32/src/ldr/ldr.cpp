#include "ldr.h"
#include <math.h>

void ldr_begin() {
    pinMode(LDR_AO_PIN, INPUT);
}

static float adcToLux(int adcValue) {
    const float GAMMA = 0.7f;
    const float RL10  = 33.0f;  // опір LDR при 10 lux (кОм)

    float voltage    = adcValue / 4096.0f * 3.3f;
    float resistance = 2000.0f * voltage / (1.0f - voltage / 3.3f);
    return pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1.0f / GAMMA));
}

float ldr_read_lux() {
    return adcToLux(analogRead(LDR_AO_PIN));
}
