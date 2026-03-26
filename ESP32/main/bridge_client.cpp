#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "konfiguracja.h"

extern TaskHandle_t g_scan_task;
void set_status(const String &s);
void set_wifi_text(const String &s);
void set_progress(uint32_t done, uint32_t total);
void append_log(const String &line);
void mark_scan_started();
void mark_scan_finished();
bool is_scan_busy();

namespace {

static bool json_get_string(const String &line, const char *key, String &out) {
  String pat = String("\"") + key + "\":\"";
  int s = line.indexOf(pat);
  if (s < 0) return false;
  s += pat.length();

  int e = s;
  while (e < (int)line.length()) {
    char c = line[(unsigned int)e];
    if (c == '\\') {
      e += 2;
      continue;
    }
    if (c == '"') break;
    e++;
  }
  if (e >= (int)line.length()) return false;

  out = line.substring((unsigned int)s, (unsigned int)e);
  return true;
}

static bool json_get_u32(const String &line, const char *key, uint32_t &out) {
  String pat = String("\"") + key + "\":";
  int s = line.indexOf(pat);
  if (s < 0) return false;
  s += pat.length();

  while (s < (int)line.length() && (line[(unsigned int)s] == ' ' || line[(unsigned int)s] == '"')) s++;

  int e = s;
  while (e < (int)line.length()) {
    char c = line[(unsigned int)e];
    if (c < '0' || c > '9') break;
    e++;
  }
  if (e <= s) return false;

  out = (uint32_t)line.substring((unsigned int)s, (unsigned int)e).toInt();
  return true;
}

static int mask_prefix(const IPAddress &mask) {
  int p = 0;
  for (int i = 0; i < 4; i++) {
    uint8_t b = mask[(unsigned int)i];
    for (int bit = 7; bit >= 0; bit--) {
      if (b & (1 << bit)) p++;
      else return p;
    }
  }
  return p;
}

static String subnet_cidr(const IPAddress &ip, const IPAddress &mask) {
  uint32_t ip_u = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
  uint32_t mask_u = ((uint32_t)mask[0] << 24) | ((uint32_t)mask[1] << 16) | ((uint32_t)mask[2] << 8) | (uint32_t)mask[3];
  uint32_t net_u = ip_u & mask_u;
  IPAddress net((uint8_t)((net_u >> 24) & 0xFF), (uint8_t)((net_u >> 16) & 0xFF),
                (uint8_t)((net_u >> 8) & 0xFF), (uint8_t)(net_u & 0xFF));
  return net.toString() + "/" + mask_prefix(mask);
}

static bool ensure_wifi(unsigned long timeout_ms) {
  if (WiFi.status() == WL_CONNECTED) return true;

  set_status("Status: laczenie WiFi...");
  set_wifi_text("WiFi: laczenie...");

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
#ifdef WIFI_POWER_8_5dBm
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (WiFi.status() != WL_CONNECTED) {
    set_wifi_text("WiFi: OFF");
    append_log("BRIDGE: brak WiFi");
    return false;
  }

  set_wifi_text(String("WiFi: ") + WiFi.localIP().toString());
  return true;
}

static void finish_bridge_task() {
  mark_scan_finished();
  vTaskDelete(nullptr);
}

static void handle_bridge_line(const String &line, bool &done, bool &error) {
  String typ;
  if (!json_get_string(line, "type", typ)) return;

  if (typ == "hello") {
    append_log("BRIDGE: polaczono");
    set_status("Status: bridge polaczony");
    return;
  }

  if (typ == "log") {
    String msg;
    if (json_get_string(line, "msg", msg)) append_log(String("BRIDGE: ") + msg);
    return;
  }

  if (typ == "host_up") {
    String ip, method;
    if (json_get_string(line, "ip", ip)) {
      if (!json_get_string(line, "method", method)) method = "?";
      append_log(String("ARP ") + ip + " UP [" + method + "]");
    }
    return;
  }

  if (typ == "port_open") {
    String ip;
    uint32_t port = 0;
    if (json_get_string(line, "ip", ip) && json_get_u32(line, "port", port)) {
      append_log(String("PORT ") + ip + ":" + port + " OPEN");
    }
    return;
  }

  if (typ == "progress") {
    String stage;
    uint32_t p_done = 0, p_total = 0, p_pct = 0, p_eta = 0;
    if (!json_get_string(line, "stage", stage)) stage = "scan";
    (void)json_get_u32(line, "done", p_done);
    (void)json_get_u32(line, "total", p_total);
    (void)json_get_u32(line, "percent", p_pct);
    (void)json_get_u32(line, "eta_s", p_eta);

    set_progress(p_done, p_total);
    String st = String("Status: bridge ") + stage + " " + p_pct + "%";
    if (p_eta > 0) st += String(" ETA ") + p_eta + "s";
    set_status(st);
    return;
  }

  if (typ == "stage") {
    String stage, status;
    (void)json_get_string(line, "stage", stage);
    (void)json_get_string(line, "status", status);
    append_log(String("BRIDGE stage ") + stage + " " + status);
    if (status == "start") set_status(String("Status: bridge ") + stage + "...");
    return;
  }

  if (typ == "done") {
    append_log("BRIDGE: done");
    set_status("Status: zakonczono bridge");
    done = true;
    return;
  }

  if (typ == "error") {
    String msg;
    if (!json_get_string(line, "msg", msg)) msg = "unknown";
    append_log(String("BRIDGE ERR: ") + msg);
    set_status("Status: bridge blad");
    error = true;
    done = true;
    return;
  }
}

static void bridge_task(void *arg) {
  (void)arg;

  mark_scan_started();
  set_progress(0, 0);
  append_log(String("BRIDGE: start ") + BRIDGE_SERVER_IP + ":" + BRIDGE_SERVER_PORT);

  if (!ensure_wifi(SCAN_WIFI_CONNECT_TIMEOUT_MS)) {
    set_status("Status: brak WiFi");
    finish_bridge_task();
    return;
  }

  WiFiClient client;
  client.setTimeout(50);
  if (!client.connect(BRIDGE_SERVER_IP, BRIDGE_SERVER_PORT)) {
    append_log("BRIDGE: nie mozna polaczyc z serwerem");
    set_status("Status: bridge offline");
    finish_bridge_task();
    return;
  }

  String subnet = subnet_cidr(WiFi.localIP(), WiFi.subnetMask());
  String req = String("{\"token\":\"") + BRIDGE_TOKEN + "\",\"cmd\":\"scan\",\"args\":{"
              "\"subnet\":\"" + subnet + "\","
              "\"port_start\":" + String(SCAN_PORT_START) + ","
              "\"port_end\":" + String(SCAN_PORT_END) + ","
              "\"timeout_ms\":" + String(SCAN_PORT_TIMEOUT_MS) + ","
              "\"workers\":" + String(BRIDGE_WORKERS) + ","
              "\"arp_timeout_ms\":" + String(BRIDGE_ARP_TIMEOUT_MS) + "}}";
  client.print(req);
  client.print("\n");

  append_log(String("BRIDGE: request subnet=") + subnet);
  set_status("Status: bridge scan start");

  String line = "";
  bool done = false;
  bool error = false;
  unsigned long last_rx = millis();

  while (!done) {
    while (client.available() > 0) {
      char c = (char)client.read();
      last_rx = millis();
      if (c == '\r') continue;
      if (c == '\n') {
        if (line.length()) {
          handle_bridge_line(line, done, error);
          line = "";
        }
        continue;
      }
      if (line.length() < 1500) line += c;
    }

    if (done) break;

    if (!client.connected() && client.available() == 0) {
      append_log("BRIDGE: polaczenie zamkniete");
      if (!error) set_status("Status: bridge rozlaczony");
      break;
    }

    if ((millis() - last_rx) > BRIDGE_IDLE_TIMEOUT_MS) {
      append_log("BRIDGE: timeout bez danych");
      if (!error) set_status("Status: bridge timeout");
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  client.stop();
  finish_bridge_task();
}

}  // namespace

bool start_bridge_scan_request(const char *source) {
  if (is_scan_busy()) {
    append_log("BRIDGE: scan juz trwa");
    return false;
  }

  append_log(source ? String(source) : String("BRIDGE: start"));
  if (xTaskCreatePinnedToCore(bridge_task, "bridge_task", 10240, nullptr, 1, &g_scan_task, 0) != pdPASS) {
    append_log("BRIDGE: blad tworzenia taska");
    return false;
  }

  return true;
}
