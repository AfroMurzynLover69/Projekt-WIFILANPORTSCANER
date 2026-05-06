#pragma once

#include <Arduino.h>

enum WifiAuthMode {
  WIFI_AUTH_PERSONAL = 0,
  WIFI_AUTH_ENTERPRISE_TTLS_PAP = 1,
  WIFI_AUTH_ENTERPRISE_PEAP_MSCHAPV2 = 2
};

struct WifiRuntimeConfig {
  bool valid = false;
  bool ap_enabled = true;
  WifiAuthMode auth_mode = WIFI_AUTH_PERSONAL;
  String ssid;
  String password;
  String username;
  String identity;
  String anonymous_identity;
};

bool wifi_config_load();
bool wifi_config_save(const WifiRuntimeConfig &cfg);
void wifi_config_set(const WifiRuntimeConfig &cfg);
void wifi_config_get(WifiRuntimeConfig &out);
bool wifi_config_is_ready();
uint32_t wifi_config_version();
String wifi_auth_mode_to_ini(WifiAuthMode mode);
String wifi_auth_mode_label(WifiAuthMode mode);
WifiAuthMode wifi_auth_mode_from_ini(const String &value);
