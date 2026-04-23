#include "scan_log_sd.h"

#include <SD_MMC.h>
#include <WiFi.h>
#include <time.h>

#include "konfiguracja.h"

namespace {

esp_expander::CH422G *g_expander = nullptr;
bool g_sd_ready = false;
bool g_ntp_started = false;
String g_last_file = "";

void set_sd_cs_level(bool high) {
  if (!g_expander) return;
  g_expander->digitalWrite(CH422G_PIN_SD_CS, high ? HIGH : LOW);
}

bool ensure_time_synced(struct tm &out_tm) {
  if (!g_ntp_started) {
    configTzTime(SCAN_LOG_TZ, SCAN_LOG_NTP_SERVER_1, SCAN_LOG_NTP_SERVER_2, SCAN_LOG_NTP_SERVER_3);
    g_ntp_started = true;
  }

  for (int i = 0; i < 40; i++) {
    if (getLocalTime(&out_tm, 100)) return true;
    delay(100);
  }
  return false;
}

String build_log_path_with_timestamp(bool *has_time_out = nullptr) {
  struct tm tm_now = {};
  bool has_time = ensure_time_synced(tm_now);
  if (has_time_out) *has_time_out = has_time;

  char buf[48] = {0};
  if (has_time) {
    strftime(buf, sizeof(buf), "/scan-%Y%m%d-%H%M%S.log", &tm_now);
  } else {
    snprintf(buf, sizeof(buf), "/scan-uptime-%lu.log", (unsigned long)millis());
  }
  return String(buf);
}

String ensure_unique_log_path(const String &base_path) {
  if (!SD_MMC.exists(base_path.c_str())) return base_path;

  int ext = base_path.lastIndexOf(".log");
  if (ext < 0) ext = (int)base_path.length();

  for (int i = 1; i <= 99; i++) {
    String candidate = base_path.substring(0, (unsigned int)ext) + "-" + i + ".log";
    if (!SD_MMC.exists(candidate.c_str())) return candidate;
  }
  return base_path;
}

}  // namespace

bool scan_log_sd_init(esp_expander::CH422G *expander) {
  g_expander = expander;
  g_sd_ready = false;

  if (!g_expander) return false;

  // For SD/MMC mode this line should stay high (card DAT3 pull-up behavior).
  set_sd_cs_level(true);
  delay(2);

  SD_MMC.end();
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) {
    return false;
  }

  // Ustawienie format_if_empty na 'true' - ESP32 sformatuje kartę do czystego FAT32, jeśli nie potrafi jej zamontować
  g_sd_ready = SD_MMC.begin(SCAN_SD_MOUNT_POINT, true, true, SCAN_SD_FREQ_HZ, SCAN_SD_MAX_OPEN_FILES);
  return g_sd_ready;
}

bool scan_log_sd_ready() {
  return g_sd_ready;
}

String scan_log_sd_status_text() {
  if (!g_sd_ready) return "SD: OFF";

  uint64_t total_mb = SD_MMC.totalBytes() / (1024ULL * 1024ULL);
  uint64_t used_mb = SD_MMC.usedBytes() / (1024ULL * 1024ULL);
  return String("SD: ON ") + used_mb + "/" + total_mb + " MB";
}

String scan_log_sd_last_file() {
  return g_last_file;
}

bool scan_log_sd_save(const ScanReportData &report) {
  if (!g_sd_ready) {
    if (!scan_log_sd_init(g_expander)) return false;
  }

  bool has_time = false;
  String path = ensure_unique_log_path(build_log_path_with_timestamp(&has_time));
  File f = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!f) return false;

  struct tm tm_now = {};
  bool got_time = has_time && getLocalTime(&tm_now, 20);
  char ts_buf[40] = {0};
  if (got_time) {
    strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M:%S %Z", &tm_now);
  } else {
    snprintf(ts_buf, sizeof(ts_buf), "unknown (NTP not synced)");
  }

  f.println("=== SCAN REPORT ===");
  f.println(String("timestamp: ") + ts_buf);
  f.println(String("wifi_ssid: ") + (report.wifi_ssid.length() ? report.wifi_ssid : "-"));
  f.println(String("scan_mode: ") + (report.scan_mode.length() ? report.scan_mode : "-"));
  f.println();

  f.println("[devices]");
  if (report.devices.length()) {
    f.println(report.devices);
  } else {
    f.println("-");
  }
  f.println();

  f.println("[open_ports]");
  if (report.ports.length()) {
    f.println(report.ports);
  } else {
    f.println("-");
  }
  f.println();

  f.println("[raw_log]");
  if (report.raw_log.length()) {
    f.println(report.raw_log);
  } else {
    f.println("-");
  }

  f.close();
  g_last_file = path;
  return true;
}
