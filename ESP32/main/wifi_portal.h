#pragma once

#include <Arduino.h>

bool wifi_portal_start();
bool wifi_portal_stop();
void wifi_portal_pump();
bool wifi_portal_running();
String wifi_portal_status_text();
