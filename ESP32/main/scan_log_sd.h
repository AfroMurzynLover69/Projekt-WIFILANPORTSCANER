#pragma once

#include <Arduino.h>
#include <esp_io_expander.hpp>

struct ScanReportData {
  String wifi_ssid;
  String scan_mode;
  String devices;
  String ports;
  String raw_log;
};

bool scan_log_sd_init(esp_expander::CH422G *expander);
bool scan_log_sd_ready();
String scan_log_sd_status_text();
String scan_log_sd_last_file();
bool scan_log_sd_save(const ScanReportData &report);
