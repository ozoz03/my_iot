#pragma once
#include <Arduino.h>

// Wi-Fi + NTP. Час потрібен ДО TLS: ESP32 стартує з 1970 року
// і handshake вважає сертифікат AWS "ще не дійсним".
bool net_wifi_connect();
bool net_wifi_connected();
bool net_time_sync();
