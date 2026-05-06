#include "wifi_config.h"

#include <SD_MMC.h>

#include "konfiguracja.h"
#include "scan_log_sd.h"

namespace {

WifiRuntimeConfig g_cfg;
uint32_t g_cfg_version = 0;

String trim_copy(String value) {
  value.trim();
  return value;
}

String ini_escape(String value) {
  value.replace("\r", "");
  value.replace("\n", "");
  return value;
}

bool parse_bool_value(const String &value, bool fallback) {
  String v = trim_copy(value);
  v.toLowerCase();
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  return fallback;
}

bool parse_bool_valid(const WifiRuntimeConfig &cfg) {
  return cfg.ssid.length() > 0;
}

void apply_pair(WifiRuntimeConfig &cfg, const String &key, const String &value) {
  if (key == "mode") cfg.auth_mode = wifi_auth_mode_from_ini(value);
  else if (key == "ap_enable" || key == "ap_enabled") cfg.ap_enabled = parse_bool_value(value, cfg.ap_enabled);
  else if (key == "ssid") cfg.ssid = value;
  else if (key == "password") cfg.password = value;
  else if (key == "username") cfg.username = value;
  else if (key == "identity") cfg.identity = value;
  else if (key == "anonymous_identity") cfg.anonymous_identity = value;
}

}  // namespace

String wifi_auth_mode_to_ini(WifiAuthMode mode) {
  switch (mode) {
    case WIFI_AUTH_ENTERPRISE_TTLS_PAP:
      return "ttls_pap";
    case WIFI_AUTH_ENTERPRISE_PEAP_MSCHAPV2:
      return "peap_mschapv2";
    case WIFI_AUTH_PERSONAL:
    default:
      return "personal";
  }
}

String wifi_auth_mode_label(WifiAuthMode mode) {
  switch (mode) {
    case WIFI_AUTH_ENTERPRISE_TTLS_PAP:
      return "Enterprise TTLS/PAP";
    case WIFI_AUTH_ENTERPRISE_PEAP_MSCHAPV2:
      return "Enterprise PEAP/MSCHAPv2";
    case WIFI_AUTH_PERSONAL:
    default:
      return "Zwykle WiFi";
  }
}

WifiAuthMode wifi_auth_mode_from_ini(const String &value) {
  String v = trim_copy(value);
  v.toLowerCase();
  if (v == "enterprise" || v == "ttls" || v == "ttls_pap") return WIFI_AUTH_ENTERPRISE_TTLS_PAP;
  if (v == "peap" || v == "peap_mschapv2") return WIFI_AUTH_ENTERPRISE_PEAP_MSCHAPV2;
  return WIFI_AUTH_PERSONAL;
}

bool wifi_config_load() {
  if (!scan_log_sd_ready()) return false;

  File f = SD_MMC.open(WIFI_CONFIG_PATH, FILE_READ);
  if (!f) return false;

  WifiRuntimeConfig cfg;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line.startsWith("#") || line.startsWith(";") || line.startsWith("[")) continue;

    int eq = line.indexOf('=');
    if (eq <= 0) continue;

    String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    key.trim();
    value.trim();
    key.toLowerCase();
    apply_pair(cfg, key, value);
  }
  f.close();

  cfg.valid = parse_bool_valid(cfg);
  wifi_config_set(cfg);
  return cfg.valid;
}

bool wifi_config_save(const WifiRuntimeConfig &cfg) {
  if (!scan_log_sd_ready()) return false;

  File f = SD_MMC.open(WIFI_CONFIG_PATH, FILE_WRITE);
  if (!f) return false;

  f.println("[wifi]");
  f.println(String("ap_enable=") + (cfg.ap_enabled ? "1" : "0"));
  f.println(String("mode=") + wifi_auth_mode_to_ini(cfg.auth_mode));
  f.println(String("ssid=") + ini_escape(cfg.ssid));
  f.println(String("password=") + ini_escape(cfg.password));
  f.println(String("username=") + ini_escape(cfg.username));
  f.println(String("identity=") + ini_escape(cfg.identity));
  f.println(String("anonymous_identity=") + ini_escape(cfg.anonymous_identity));
  f.close();
  return true;
}

void wifi_config_set(const WifiRuntimeConfig &cfg) {
  g_cfg = cfg;
  g_cfg.valid = parse_bool_valid(g_cfg);
  g_cfg_version++;
}

void wifi_config_get(WifiRuntimeConfig &out) {
  out = g_cfg;
}

bool wifi_config_is_ready() {
  return g_cfg.valid;
}

uint32_t wifi_config_version() {
  return g_cfg_version;
}
