#pragma once

#include <Arduino.h>

bool sd_ota_file_available();
String sd_ota_file_path();
String sd_ota_file_info();
bool sd_ota_apply_from_sd(String &error_out);
