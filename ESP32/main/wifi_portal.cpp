#include "wifi_portal.h"

#include <WebServer.h>
#include <WiFi.h>

#include "konfiguracja.h"
#include "scan_log_sd.h"
#include "wifi_config.h"

namespace {

WebServer g_server(80);
bool g_running = false;
String g_last_save_status;

String html_escape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

bool require_auth() {
  if (g_server.authenticate(WIFI_CONFIG_WEB_USER, WIFI_CONFIG_WEB_PASS)) return true;
  g_server.requestAuthentication();
  return false;
}

String selected_attr(WifiAuthMode current, WifiAuthMode option) {
  return current == option ? " selected" : "";
}

String render_network_options() {
  String out;
  int count = WiFi.scanNetworks(false, true);
  if (count <= 0) {
    out += "<p class='muted'>No networks found or scan is temporarily unavailable.</p>";
    return out;
  }

  out += "<datalist id='ssids'>";
  for (int i = 0; i < count; i++) {
    out += "<option value='";
    out += html_escape(WiFi.SSID(i));
    out += "'></option>";
  }
  out += "</datalist><table><tr><th>SSID</th><th>RSSI</th><th>Zabezp.</th></tr>";
  for (int i = 0; i < count; i++) {
    out += "<tr><td>";
    out += html_escape(WiFi.SSID(i));
    out += "</td><td>";
    out += String(WiFi.RSSI(i));
    out += " dBm</td><td>";
    out += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "password/enterprise";
    out += "</td></tr>";
  }
  out += "</table>";
  WiFi.scanDelete();
  return out;
}

void handle_root() {
  if (!require_auth()) return;

  WifiRuntimeConfig cfg;
  wifi_config_get(cfg);

  IPAddress sta_ip = WiFi.localIP();
  IPAddress ap_ip = WiFi.softAPIP();
  String sd_status = scan_log_sd_status_text();

  String html;
  html.reserve(9000);
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESP32 WiFi config</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:24px;background:#f5f5f5;color:#111}";
  html += "main{max-width:760px;margin:auto;background:#fff;border:1px solid #ccc;padding:18px}";
  html += "label{display:block;margin-top:12px;font-weight:700}input,select,button{font:inherit;width:100%;box-sizing:border-box;padding:9px;margin-top:4px}";
  html += "button{cursor:pointer;font-weight:700}table{width:100%;border-collapse:collapse;margin-top:14px}td,th{border:1px solid #ddd;padding:6px;text-align:left}";
  html += ".muted{color:#666}.ok{background:#e7f7e7;border:1px solid #8ac78a;padding:8px}.bad{background:#fff0d8;border:1px solid #d7ad60;padding:8px}";
  html += "</style></head><body><main>";
  html += "<h1>ESP32 WiFi config</h1>";
  html += "<p>Panel: <b>admin/admin</b>. AP: <b>";
  html += html_escape(WIFI_CONFIG_AP_SSID);
  html += "</b>, AP password: <b>";
  html += html_escape(WIFI_CONFIG_AP_PASS);
  html += "</b>.</p>";
  html += "<p class='muted'>AP IP: ";
  html += ap_ip.toString();
  html += " | STA IP: ";
  html += sta_ip.toString();
  html += " | ";
  html += html_escape(sd_status);
  html += "</p>";

  if (g_last_save_status.length()) {
    html += scan_log_sd_ready() ? "<p class='ok'>" : "<p class='bad'>";
    html += html_escape(g_last_save_status);
    html += "</p>";
  }
  if (!scan_log_sd_ready()) {
    html += "<p class='bad'>SD unavailable: settings will work only until restart and will not be saved to config.ini.</p>";
  }

  html += "<form method='post' action='/save'>";
  html += "<label>Tryb</label><select name='mode'>";
  html += "<option value='personal'";
  html += selected_attr(cfg.auth_mode, WIFI_AUTH_PERSONAL);
  html += ">Standard WiFi (SSID + password)</option>";
  html += "<option value='ttls_pap'";
  html += selected_attr(cfg.auth_mode, WIFI_AUTH_ENTERPRISE_TTLS_PAP);
  html += ">Enterprise TTLS/PAP (np. eduroam)</option>";
  html += "<option value='peap_mschapv2'";
  html += selected_attr(cfg.auth_mode, WIFI_AUTH_ENTERPRISE_PEAP_MSCHAPV2);
  html += ">Enterprise PEAP/MSCHAPv2</option>";
  html += "</select>";
  html += "<label>SSID</label><input name='ssid' list='ssids' value='";
  html += html_escape(cfg.ssid);
  html += "' required>";
  html += "<label>WiFi / Enterprise password</label><input name='password' type='password' placeholder='leave empty to keep current value'>";
  html += "<label>Login enterprise</label><input name='username' value='";
  html += html_escape(cfg.username);
  html += "' placeholder='np. s000000@student.tu.kielce.pl'>";
  html += "<label>Enterprise identity</label><input name='identity' value='";
  html += html_escape(cfg.identity);
  html += "' placeholder='usually the same as login'>";
  html += "<label>Anonymous identity</label><input name='anonymous_identity' value='";
  html += html_escape(cfg.anonymous_identity);
  html += "' placeholder='opcjonalnie'>";
  html += "<button type='submit'>Save config.ini and connect</button></form>";
  html += "<h2>Detected networks</h2>";
  html += render_network_options();
  html += "</main></body></html>";

  g_server.send(200, "text/html; charset=utf-8", html);
}

void handle_save() {
  if (!require_auth()) return;

  WifiRuntimeConfig old_cfg;
  wifi_config_get(old_cfg);

  WifiRuntimeConfig cfg;
  cfg.auth_mode = wifi_auth_mode_from_ini(g_server.arg("mode"));
  cfg.ssid = g_server.arg("ssid");
  cfg.ssid.trim();
  cfg.password = g_server.arg("password");
  if (!cfg.password.length()) cfg.password = old_cfg.password;
  cfg.username = g_server.arg("username");
  cfg.username.trim();
  cfg.identity = g_server.arg("identity");
  cfg.identity.trim();
  cfg.anonymous_identity = g_server.arg("anonymous_identity");
  cfg.anonymous_identity.trim();
  if (!cfg.identity.length()) cfg.identity = cfg.username;
  if (!cfg.anonymous_identity.length()) cfg.anonymous_identity = cfg.identity;
  cfg.valid = cfg.ssid.length() > 0;

  wifi_config_set(cfg);
  bool saved = wifi_config_save(cfg);
  g_last_save_status = saved ? "Saved /config.ini. ESP is trying to connect to the selected network."
                             : "Not saved to SD. Configuration is only in RAM until restart.";

  g_server.sendHeader("Location", "/");
  g_server.send(303);
}

void handle_not_found() {
  if (!require_auth()) return;
  g_server.send(404, "text/plain; charset=utf-8", "404");
}

}  // namespace

bool wifi_portal_start() {
  if (g_running) return true;

  WiFi.mode(WIFI_AP_STA);
  bool ap_ok = WiFi.softAP(WIFI_CONFIG_AP_SSID, WIFI_CONFIG_AP_PASS);
  if (!ap_ok) return false;

  g_server.on("/", HTTP_GET, handle_root);
  g_server.on("/save", HTTP_POST, handle_save);
  g_server.onNotFound(handle_not_found);
  g_server.begin();
  g_running = true;
  return true;
}

bool wifi_portal_stop() {
  if (!g_running) return true;

  g_server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_running = false;
  return true;
}

void wifi_portal_pump() {
  if (g_running) g_server.handleClient();
}

bool wifi_portal_running() {
  return g_running;
}

String wifi_portal_status_text() {
  if (!g_running) return "AP: OFF";
  return String("AP: ") + WIFI_CONFIG_AP_SSID + " " + WiFi.softAPIP().toString();
}
