#include "gui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#define LV_CONF_PATH "lv_conf.h"
#include <lvgl.h>
#include <vector>
#include <time.h>
#include <math.h>

#include "konfiguracja.h"
#include "scan_log_sd.h"
#include "sd_ota.h"
#include "wifi_config.h"
#include "wifi_portal.h"

struct AppSnapshot {
  String wifi;
  String status;
  String log;
  uint32_t done;
  uint32_t total;
  bool running;
};

bool start_scan_request(const char *source);
void snapshot_state(AppSnapshot &out);
bool is_scan_busy();
void set_port_profile_legacy();
void set_port_profile_full();
bool is_port_profile_legacy();
void set_wifi_text(const String &s);
void set_status(const String &s);
void append_log(const String &line);
const char *scan_mode_name();

enum StepState {
  STEP_PENDING,
  STEP_ACTIVE,
  STEP_DONE,
  STEP_SKIPPED,
};

enum StageId {
  STAGE_NONE,
  STAGE_START,
  STAGE_DISCOVER,
  STAGE_PORTS,
  STAGE_FINISH,
};

enum PageId {
  PAGE_MAIN,
  PAGE_LOG,
  PAGE_PORTS,
};

struct ScanUiModel {
  StepState start_state;
  StepState discover_state;
  StepState ports_state;
  StepState finish_state;
  bool error;
  bool finished;
  uint8_t progress_percent;
  const char *short_status;
};

static constexpr uint32_t INSTALL_BLOCKS = 24;
static constexpr lv_coord_t HEADER_LEFT_W = 520;
static constexpr lv_coord_t HEADER_RIGHT_W = 420;
static constexpr lv_coord_t STATUS_LEFT_W = 560;
static constexpr lv_coord_t STATUS_RIGHT_W = 380;

static const lv_font_t *FONT_TITLE = &lv_font_montserrat_20;
static const lv_font_t *FONT_BIG = &lv_font_montserrat_18;
static const lv_font_t *FONT_BODY = &lv_font_montserrat_16;

static String g_ui_last_wifi = "";
static String g_ui_last_ap_ip = "";
static String g_ui_last_status = "";
static String g_ui_last_log = "";
static String g_ui_last_open_ports = "";
static int g_ui_last_percent = -1;
static int g_ui_last_bar_percent = -1;
static int g_ui_last_start_state = -1;
static int g_ui_last_discover_state = -1;
static int g_ui_last_ports_state = -1;
static int g_ui_last_finish_state = -1;
static int g_ui_last_finish_error = -1;
static int g_ui_last_wifi_ok = -1;
static int g_ui_last_ap_ok = -1;
static int g_ui_last_sd_ok = -1;
static uint32_t g_ui_last_done = UINT32_MAX;
static uint32_t g_ui_last_total = UINT32_MAX;

static lv_obj_t *g_page_main = nullptr;
static lv_obj_t *g_page_log = nullptr;
static lv_obj_t *g_page_ports = nullptr;

static lv_obj_t *g_lbl_title = nullptr;
static lv_obj_t *g_lbl_hint_main = nullptr;
static lv_obj_t *g_lbl_status = nullptr;

static lv_obj_t *g_card_start = nullptr;
static lv_obj_t *g_card_discover = nullptr;
static lv_obj_t *g_card_ports = nullptr;
static lv_obj_t *g_card_finish = nullptr;

static lv_obj_t *g_lbl_start_state = nullptr;
static lv_obj_t *g_lbl_discover_state = nullptr;
static lv_obj_t *g_lbl_ports_state = nullptr;
static lv_obj_t *g_lbl_finish_state = nullptr;

static lv_obj_t *g_panel_progress = nullptr;
static lv_obj_t *g_bar_strip = nullptr;
static lv_obj_t *g_install_blocks[INSTALL_BLOCKS] = {nullptr};
static lv_obj_t *g_lbl_progress = nullptr;

static lv_obj_t *g_btn_ports_legacy = nullptr;
static lv_obj_t *g_btn_ports_full = nullptr;
static lv_obj_t *g_btn_start = nullptr;

static lv_obj_t *g_btn_theme_dark = nullptr;
static lv_obj_t *g_btn_theme_light = nullptr;
static lv_obj_t *g_btn_sd_ota = nullptr;
static lv_obj_t *g_btn_ap_toggle = nullptr;
static lv_obj_t *g_btn_sd_wipe = nullptr;

static lv_obj_t *g_btn_nav_log = nullptr;
static lv_obj_t *g_btn_nav_ports = nullptr;
static lv_obj_t *g_btn_back_log = nullptr;
static lv_obj_t *g_btn_back_ports = nullptr;

static lv_obj_t *g_lbl_log_title = nullptr;
static lv_obj_t *g_lbl_hint_log = nullptr;
static lv_obj_t *g_lbl_log_status = nullptr;
static lv_obj_t *g_panel_log = nullptr;
static lv_obj_t *g_lbl_log = nullptr;

static lv_obj_t *g_lbl_ports_title = nullptr;
static lv_obj_t *g_lbl_hint_ports = nullptr;
static lv_obj_t *g_lbl_ports_status = nullptr;
static lv_obj_t *g_panel_ports = nullptr;
static lv_obj_t *g_lbl_open_ports = nullptr;

static lv_obj_t *g_lbl_wifi_icon_main = nullptr;
static lv_obj_t *g_lbl_ap_icon_main = nullptr;
static lv_obj_t *g_lbl_sd_icon_main = nullptr;
static lv_obj_t *g_lbl_wifi_icon_log = nullptr;
static lv_obj_t *g_lbl_ap_icon_log = nullptr;
static lv_obj_t *g_lbl_sd_icon_log = nullptr;
static lv_obj_t *g_lbl_wifi_icon_ports = nullptr;
static lv_obj_t *g_lbl_ap_icon_ports = nullptr;
static lv_obj_t *g_lbl_sd_icon_ports = nullptr;

static lv_obj_t *g_lbl_top_ip_main = nullptr;
static lv_obj_t *g_lbl_top_ip_log = nullptr;
static lv_obj_t *g_lbl_top_ip_ports = nullptr;
static lv_obj_t *g_lbl_top_ap_ip_main = nullptr;
static lv_obj_t *g_lbl_top_ap_ip_log = nullptr;
static lv_obj_t *g_lbl_top_ap_ip_ports = nullptr;
static lv_obj_t *g_ota_prompt = nullptr;

static StageId g_stage_active = STAGE_NONE;
static unsigned long g_stage_start_ms = 0;
static PageId g_current_page = PAGE_MAIN;

static bool g_theme_light = false;

static lv_color_t get_theme_bg() { return lv_color_hex(g_theme_light ? 0xF0F8FF : 0x000000); }
static lv_color_t get_theme_text() { return lv_color_hex(g_theme_light ? 0x1E3A8A : 0xE5E7EB); }
static lv_color_t get_theme_text_muted() { return lv_color_hex(g_theme_light ? 0x64748B : 0x94A3B8); }
static lv_color_t get_theme_text_log() { return lv_color_hex(g_theme_light ? 0x0284C7 : 0x60A5FA); }

static void show_sd_ota_status_prompt();

static String colorize_port(const String &port_s) {
  int port = port_s.toInt();
  float h = ((float)port * 360.0f) / 65535.0f;
  float s = 0.7f;
  float v = 1.0f;
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; b = 0; }
  else if (h < 120) { r = x; g = c; b = 0; }
  else if (h < 180) { r = 0; g = c; b = x; }
  else if (h < 240) { r = 0; g = x; b = c; }
  else if (h < 300) { r = x; g = 0; b = c; }
  else { r = c; g = 0; b = x; }

  uint8_t R = (uint8_t)((r + m) * 255.0f);
  uint8_t G = (uint8_t)((g + m) * 255.0f);
  uint8_t B = (uint8_t)((b + m) * 255.0f);

  char buf[32];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X %s#", R, G, B, port_s.c_str());
  return String(buf);
}

static String extract_recent_lines(const String &log, uint32_t max_lines) {
  if (!log.length() || max_lines == 0) return "";

  int pos = (int)log.length() - 1;
  uint32_t lines = 0;
  while (pos >= 0) {
    if (log[(unsigned int)pos] == '\n') {
      lines++;
      if (lines > max_lines) {
        pos++;
        break;
      }
    }
    pos--;
  }

  if (pos < 0) pos = 0;
  return log.substring((unsigned int)pos);
}

static String collect_open_ports_by_ip(const String &log) {
  std::vector<String> ips;
  std::vector<String> seen_ports;
  std::vector<String> list_ports;

  int pos = 0;
  while (true) {
    int marker = log.indexOf("PORT ", pos);
    if (marker < 0) break;

    int payload_start = marker + 5;
    int payload_end = log.indexOf(" OPEN", payload_start);
    if (payload_end < 0) {
      pos = payload_start;
      continue;
    }

    String payload = log.substring(payload_start, payload_end);
    int sep = payload.lastIndexOf(':');
    if (sep <= 0 || sep >= (int)payload.length() - 1) {
      pos = payload_end + 5;
      continue;
    }

    String ip = payload.substring(0, sep);
    String port_s = payload.substring(sep + 1);
    int port = port_s.toInt();
    bool port_valid = (port_s.length() > 0 && port >= 0 && port <= 65535);
    if (!port_valid || ip.length() < 7 || ip.length() > 15) {
      pos = payload_end + 5;
      continue;
    }

    int idx = -1;
    for (unsigned int i = 0; i < ips.size(); i++) {
      if (ips[i] == ip) {
        idx = (int)i;
        break;
      }
    }

    if (idx < 0) {
      ips.push_back(ip);
      seen_ports.push_back("|");
      list_ports.push_back("");
      idx = (int)ips.size() - 1;
    }

    String key = String("|") + port_s + "|";
    if (seen_ports[(unsigned int)idx].indexOf(key) < 0) {
      seen_ports[(unsigned int)idx] += port_s;
      seen_ports[(unsigned int)idx] += "|";
      if (list_ports[(unsigned int)idx].length()) list_ports[(unsigned int)idx] += ", ";
      list_ports[(unsigned int)idx] += colorize_port(port_s);
    }

    pos = payload_end + 5;
  }

  String out = "";
  for (unsigned int i = 0; i < ips.size(); i++) {
    if (out.length()) out += "\n";
    out += String(i + 1) + ". " + ips[i] + " - [ ";
    out += list_ports[i].length() ? list_ports[i] : "-";
    out += " ]";
  }
  return out;
}

static int count_lines(const String &text) {
  if (!text.length()) return 0;
  int n = 1;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (text[i] == '\n') n++;
  }
  return n;
}

static float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static void set_flat_style(lv_obj_t *obj, lv_coord_t pad_all, lv_opa_t bg_opa) {
  if (!obj) return;

  lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(obj, bg_opa, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, pad_all, 0);

  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
  lv_obj_set_style_border_width(obj, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_outline_width(obj, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_SCROLLBAR);
}

static void set_bg_flat_style(lv_obj_t *obj, lv_coord_t pad_all) {
  if (!obj) return;
  lv_color_t bg = get_theme_bg();
  lv_obj_set_style_bg_color(obj, bg, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_border_color(obj, bg, 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, pad_all, 0);

  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
  lv_obj_set_style_border_width(obj, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_outline_width(obj, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_shadow_width(obj, 0, LV_PART_SCROLLBAR);
}

static void set_transparent_flat_style(lv_obj_t *obj, lv_coord_t pad_all) {
  set_flat_style(obj, pad_all, LV_OPA_TRANSP);
}

static void set_page(PageId page, lv_scr_load_anim_t anim) {
  lv_obj_t *target = nullptr;
  if (page == PAGE_MAIN) target = g_page_main;
  else if (page == PAGE_LOG) target = g_page_log;
  else if (page == PAGE_PORTS) target = g_page_ports;

  if (target) {
    if (anim == LV_SCR_LOAD_ANIM_NONE) lv_scr_load(target);
    else lv_scr_load_anim(target, anim, 250, 0, false);
  }

  g_current_page = page;
}

static void ev_nav_log(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  set_page(PAGE_LOG, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

static void ev_nav_ports(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  set_page(PAGE_PORTS, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

static void ev_nav_main(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  set_page(PAGE_MAIN, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

static void ev_start_scan(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  bool ok = start_scan_request("UI: start local scan");
  if (!ok) {
    set_status("Status: scan already running");
    append_log("UI: local start rejected");
  } else {
    set_status("Status: starting local scan...");
  }
}

static void ev_ota_no(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_ota_prompt) {
    lv_obj_delete(g_ota_prompt);
    g_ota_prompt = nullptr;
  }
  append_log("OTA: skipped SD update");
}

static void ev_ota_ok(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_ota_prompt) {
    lv_obj_delete(g_ota_prompt);
    g_ota_prompt = nullptr;
  }
}

static void ev_ota_yes(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_ota_prompt) {
    lv_obj_delete(g_ota_prompt);
    g_ota_prompt = nullptr;
  }

  set_status("Status: SD OTA...");
  append_log(String("OTA: start ") + sd_ota_file_path());
  String err;
  if (sd_ota_apply_from_sd(err)) {
    append_log("OTA: success, reboot");
    delay(250);
    ESP.restart();
  }

  set_status("Status: OTA error");
  append_log(String("OTA: error - ") + err);
}

static void ev_sd_ota(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!sd_ota_file_available()) {
    set_status("Status: SD OTA not found");
    append_log(String("OTA: file missing - ") + sd_ota_file_info());
    show_sd_ota_status_prompt();
    return;
  }
  show_sd_ota_prompt_if_available();
}

static void set_ap_enabled(bool enabled, const char *source) {
  WifiRuntimeConfig cfg;
  wifi_config_get(cfg);
  cfg.ap_enabled = enabled;
  wifi_config_set(cfg);
  bool saved = wifi_config_save(cfg);
  if (enabled) {
    if (wifi_portal_start()) {
      set_status("Status: AP enabled");
      append_log(String(source) + " AP enabled");
    } else {
      set_status("Status: AP start failed");
      append_log(String(source) + " AP start failed");
    }
  } else {
    (void)wifi_portal_stop();
    set_status("Status: AP disabled");
    append_log(String(source) + " AP disabled");
  }

  if (!saved) {
    append_log("CFG: failed to save AP setting");
  }
}

static void ev_ap_toggle(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  set_ap_enabled(!wifi_portal_running(), "UI:");
}

static void ev_sd_wipe(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  set_status("Status: SD wipe...");
  append_log("SD: wipe requested");
  if (scan_log_sd_wipe()) {
    set_status("Status: SD wipe OK");
    append_log("SD: wipe OK");
  } else {
    set_status("Status: SD wipe failed");
    append_log("SD: wipe failed");
  }
}

static void update_port_profile_buttons() {
  bool legacy = is_port_profile_legacy();
  static bool blink_state = false;
  static uint32_t last_blink = 0;

  bool need_update = false;
  if (millis() - last_blink > 200) {
    last_blink = millis();
    blink_state = !blink_state;
    need_update = true;
  }

  static int8_t s_last_legacy = -1;
  if (s_last_legacy != (legacy ? 1 : 0)) {
    s_last_legacy = legacy ? 1 : 0;
    need_update = true;
    blink_state = true;
  }

  if (!need_update) return;

  lv_color_t color_on = g_theme_light ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
  lv_color_t color_off = get_theme_bg();
  lv_color_t color_unselected = get_theme_text_muted();

  if (g_btn_ports_legacy) {
    lv_obj_t *lbl = lv_obj_get_child(g_btn_ports_legacy, 0);
    if (lbl) lv_obj_set_style_text_color(lbl, legacy ? (blink_state ? color_on : color_off) : color_unselected, 0);
  }
  if (g_btn_ports_full) {
    lv_obj_t *lbl = lv_obj_get_child(g_btn_ports_full, 0);
    if (lbl) lv_obj_set_style_text_color(lbl, !legacy ? (blink_state ? color_on : color_off) : color_unselected, 0);
  }
}

static void ev_set_ports_legacy(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (is_scan_busy()) {
    set_status("Status: port change locked");
    append_log("UI: cannot change ports during scan");
    return;
  }
  set_port_profile_legacy();
  Preferences prefs;
  prefs.begin("scanner", false);
  prefs.putBool("ports_legacy", true);
  prefs.end();
  set_status("Status: ports LIMITED");
  append_log("UI: ports profile LIMITED");
  update_port_profile_buttons();
}

static void ev_set_ports_full(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (is_scan_busy()) {
    set_status("Status: port change locked");
    append_log("UI: cannot change ports during scan");
    return;
  }
  set_port_profile_full();
  Preferences prefs;
  prefs.begin("scanner", false);
  prefs.putBool("ports_legacy", false);
  prefs.end();
  set_status("Status: ports FULL");
  append_log("UI: ports profile FULL");
  update_port_profile_buttons();
}

static lv_obj_t *create_step_card(lv_obj_t *parent, int32_t x, int32_t y, const char *title, lv_obj_t **state_lbl_out) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 220, 170);
  lv_obj_set_pos(card, x, y);
  set_transparent_flat_style(card, 10);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_title = lv_label_create(card);
  lv_label_set_text(lbl_title, title);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_title, FONT_BIG, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *lbl_state = lv_label_create(card);
  lv_label_set_text(lbl_state, "WAIT");
  lv_obj_set_style_text_color(lbl_state, lv_color_hex(0x93A4B8), 0);
  lv_obj_set_style_text_font(lbl_state, FONT_BIG, 0);
  lv_obj_align(lbl_state, LV_ALIGN_CENTER, 0, 10);

  if (state_lbl_out) *state_lbl_out = lbl_state;
  return card;
}

static void set_step_card_state(lv_obj_t *card, lv_obj_t *state_lbl, StepState state, bool mark_error) {
  if (!card || !state_lbl) return;

  lv_color_t text = get_theme_text_muted();
  const char *state_word = "WAIT";

  if (state == STEP_ACTIVE) {
    text = get_theme_text();
    state_word = "RUN";
  } else if (state == STEP_DONE) {
    text = get_theme_text();
    state_word = "OK";
  } else if (state == STEP_SKIPPED) {
    text = get_theme_text_muted();
    state_word = "SKIP";
  }

  if (mark_error && state == STEP_DONE) {
    text = get_theme_text();
    state_word = "ERR";
  }

  set_transparent_flat_style(card, 10);
  lv_label_set_text(state_lbl, state_word);
  lv_obj_set_style_text_color(state_lbl, text, 0);
}

static void set_install_bar_percent(uint8_t pct) {
  uint32_t active_count = ((uint32_t)pct * INSTALL_BLOCKS) / 100;
  lv_color_t color_on = lv_color_hex(0x22C55E); // Zielony
  lv_color_t color_off = g_theme_light ? lv_color_hex(0xE2E8F0) : lv_color_hex(0x334155);

  for (uint32_t i = 0; i < INSTALL_BLOCKS; i++) {
    if (!g_install_blocks[i]) continue;
    
    lv_obj_set_style_bg_color(g_install_blocks[i], i < active_count ? color_on : color_off, 0);
    lv_obj_set_style_bg_opa(g_install_blocks[i], LV_OPA_COVER, 0);
  }

  if (g_lbl_progress) {
    String text = String((int)pct) + "%";
    lv_label_set_text(g_lbl_progress, text.c_str());
  }
}

static void enforce_theme_transparent_visuals() {
  if (g_page_main) set_bg_flat_style(g_page_main, 0);
  if (g_page_log) set_bg_flat_style(g_page_log, 0);
  if (g_page_ports) set_bg_flat_style(g_page_ports, 0);

  if (g_card_start) set_transparent_flat_style(g_card_start, 10);
  if (g_card_discover) set_transparent_flat_style(g_card_discover, 10);
  if (g_card_ports) set_transparent_flat_style(g_card_ports, 10);
  if (g_card_finish) set_transparent_flat_style(g_card_finish, 10);

  if (g_panel_progress) set_transparent_flat_style(g_panel_progress, 12);
  if (g_bar_strip) set_transparent_flat_style(g_bar_strip, 0);

  if (g_btn_ports_legacy) set_transparent_flat_style(g_btn_ports_legacy, 0);
  if (g_btn_ports_full) set_transparent_flat_style(g_btn_ports_full, 0);
  if (g_btn_start) set_transparent_flat_style(g_btn_start, 0);

  if (g_btn_nav_ports) set_transparent_flat_style(g_btn_nav_ports, 0);
  if (g_btn_nav_log) set_transparent_flat_style(g_btn_nav_log, 0);
  if (g_btn_back_log) set_transparent_flat_style(g_btn_back_log, 0);
  if (g_btn_back_ports) set_transparent_flat_style(g_btn_back_ports, 0);
  if (g_btn_theme_dark) set_transparent_flat_style(g_btn_theme_dark, 0);
  if (g_btn_theme_light) set_transparent_flat_style(g_btn_theme_light, 0);
  if (g_btn_sd_ota) set_transparent_flat_style(g_btn_sd_ota, 0);
  if (g_btn_ap_toggle) set_transparent_flat_style(g_btn_ap_toggle, 0);
  if (g_btn_sd_wipe) set_transparent_flat_style(g_btn_sd_wipe, 0);

  if (g_panel_log) {
    set_transparent_flat_style(g_panel_log, 10);
    lv_obj_set_scrollbar_mode(g_panel_log, LV_SCROLLBAR_MODE_OFF);
  }
  if (g_panel_ports) {
    set_transparent_flat_style(g_panel_ports, 10);
    lv_obj_set_scrollbar_mode(g_panel_ports, LV_SCROLLBAR_MODE_OFF);
  }
}

static void apply_theme() {
  lv_color_t bg = get_theme_bg();
  lv_color_t txt = get_theme_text();
  lv_color_t mut = get_theme_text_muted();
  lv_color_t logc = get_theme_text_log();

  enforce_theme_transparent_visuals();

  if (g_lbl_hint_main) lv_obj_set_style_bg_color(g_lbl_hint_main, bg, 0);
  if (g_lbl_hint_log) lv_obj_set_style_bg_color(g_lbl_hint_log, bg, 0);
  if (g_lbl_hint_ports) lv_obj_set_style_bg_color(g_lbl_hint_ports, bg, 0);

  if (g_lbl_title) lv_obj_set_style_text_color(g_lbl_title, txt, 0);
  if (g_lbl_log_title) lv_obj_set_style_text_color(g_lbl_log_title, txt, 0);
  if (g_lbl_ports_title) lv_obj_set_style_text_color(g_lbl_ports_title, txt, 0);

  if (g_lbl_hint_main) lv_obj_set_style_text_color(g_lbl_hint_main, mut, 0);
  if (g_lbl_hint_log) lv_obj_set_style_text_color(g_lbl_hint_log, mut, 0);
  if (g_lbl_hint_ports) lv_obj_set_style_text_color(g_lbl_hint_ports, mut, 0);

  if (g_lbl_status) lv_obj_set_style_text_color(g_lbl_status, txt, 0);
  if (g_lbl_log_status) lv_obj_set_style_text_color(g_lbl_log_status, mut, 0);
  if (g_lbl_ports_status) lv_obj_set_style_text_color(g_lbl_ports_status, mut, 0);
  if (g_lbl_top_ip_main) lv_obj_set_style_text_color(g_lbl_top_ip_main, txt, 0);
  if (g_lbl_top_ip_log) lv_obj_set_style_text_color(g_lbl_top_ip_log, txt, 0);
  if (g_lbl_top_ip_ports) lv_obj_set_style_text_color(g_lbl_top_ip_ports, txt, 0);
  if (g_lbl_top_ap_ip_main) lv_obj_set_style_text_color(g_lbl_top_ap_ip_main, txt, 0);
  if (g_lbl_top_ap_ip_log) lv_obj_set_style_text_color(g_lbl_top_ap_ip_log, txt, 0);
  if (g_lbl_top_ap_ip_ports) lv_obj_set_style_text_color(g_lbl_top_ap_ip_ports, txt, 0);

  if (g_lbl_log) lv_obj_set_style_text_color(g_lbl_log, logc, 0);
  if (g_lbl_open_ports) lv_obj_set_style_text_color(g_lbl_open_ports, mut, 0);
  if (g_lbl_progress) lv_obj_set_style_text_color(g_lbl_progress, txt, 0);

  auto set_child_color = [&](lv_obj_t *parent, lv_color_t color) {
    if (parent && lv_obj_get_child_cnt(parent) > 0) {
      lv_obj_t *lbl = lv_obj_get_child(parent, 0);
      if (lbl) lv_obj_set_style_text_color(lbl, color, 0);
    }
  };

  set_child_color(g_card_start, txt);
  set_child_color(g_card_discover, txt);
  set_child_color(g_card_ports, txt);
  set_child_color(g_card_finish, txt);
  set_child_color(g_panel_progress, txt);
  set_child_color(g_panel_log, txt);
  set_child_color(g_panel_ports, txt);
  set_child_color(g_btn_start, txt);
  set_child_color(g_btn_nav_ports, txt);
  set_child_color(g_btn_nav_log, txt);
  set_child_color(g_btn_back_log, txt);
  set_child_color(g_btn_back_ports, txt);
  set_child_color(g_btn_theme_dark, txt);
  set_child_color(g_btn_theme_light, txt);
  set_child_color(g_btn_sd_ota, txt);
  set_child_color(g_btn_ap_toggle, txt);
  set_child_color(g_btn_sd_wipe, txt);

  g_ui_last_start_state = -1;
  g_ui_last_discover_state = -1;
  g_ui_last_ports_state = -1;
  g_ui_last_finish_state = -1;
  update_port_profile_buttons();
  set_install_bar_percent(g_ui_last_bar_percent >= 0 ? g_ui_last_bar_percent : 0);
}

static void ev_set_theme_dark(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (!g_theme_light) return;
  g_theme_light = false;
  Preferences prefs;
  prefs.begin("scanner", false);
  prefs.putBool("theme_light", false);
  prefs.end();
  apply_theme();
}

static void ev_set_theme_light(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_theme_light) return;
  g_theme_light = true;
  Preferences prefs;
  prefs.begin("scanner", false);
  prefs.putBool("theme_light", true);
  prefs.end();
  apply_theme();
}

static ScanUiModel compute_ui_model(const AppSnapshot &s) {
  ScanUiModel m = {};
  m.start_state = STEP_PENDING;
  m.discover_state = STEP_PENDING;
  m.ports_state = STEP_PENDING;
  m.finish_state = STEP_PENDING;
  m.error = false;
  m.finished = false;
  m.progress_percent = 0;
  m.short_status = "IDLE";

  unsigned long now = millis();
  String st = s.status;
  st.toLowerCase();
  const String log = s.log;

  bool mode_has_ping = String(scan_mode_name()).indexOf("BCAST") >= 0;
  bool wifi_ok = s.wifi.startsWith("WiFi: ") && !s.wifi.startsWith("WiFi: OFF") &&
                 !s.wifi.startsWith("WiFi: connecting");
  bool scan_started = s.running || (log.indexOf("SCAN: start (") >= 0);
  bool ping_wait_done = (log.indexOf("BCAST PING: waited") >= 0);
  bool arp_started = (log.indexOf("DISCOVER: start (ARP") >= 0) || (st.indexOf("arp") >= 0);
  bool portscan_started = (log.indexOf("PORTSCAN: start") >= 0) || (st.indexOf("port scan") >= 0);
  bool portscan_skipped = (log.indexOf("PORTSCAN: skipped") >= 0);
  bool portscan_done = (log.indexOf("PORTSCAN: done") >= 0) || portscan_skipped;
  bool finished = (st.indexOf("done") >= 0) || (st.indexOf("timeout") >= 0);
  bool error = (st.indexOf("missing wifi") >= 0) || (st.indexOf("error") >= 0) ||
               (st.indexOf("timeout") >= 0);

  bool start_phase_done = wifi_ok && (!mode_has_ping || ping_wait_done || arp_started || portscan_started || finished);
  bool start_phase_active = scan_started && !start_phase_done;

  if (start_phase_done) m.start_state = STEP_DONE;
  else if (start_phase_active) m.start_state = STEP_ACTIVE;

  if (arp_started && !portscan_started && !finished) m.discover_state = STEP_ACTIVE;
  if (arp_started && (portscan_started || finished || !s.running)) m.discover_state = STEP_DONE;

  if (portscan_skipped) m.ports_state = STEP_SKIPPED;
  else if (portscan_started && !portscan_done && !finished) m.ports_state = STEP_ACTIVE;
  else if (portscan_done || (portscan_started && !s.running)) m.ports_state = STEP_DONE;

  if (finished || error) m.finish_state = STEP_DONE;
  else if (s.running && (m.discover_state == STEP_ACTIVE || m.ports_state == STEP_ACTIVE)) m.finish_state = STEP_ACTIVE;

  StageId stage = STAGE_NONE;
  if (m.finish_state == STEP_DONE) stage = STAGE_FINISH;
  else if (m.ports_state == STEP_ACTIVE) stage = STAGE_PORTS;
  else if (m.discover_state == STEP_ACTIVE) stage = STAGE_DISCOVER;
  else if (m.start_state == STEP_ACTIVE) stage = STAGE_START;

  if (stage != g_stage_active) {
    g_stage_active = stage;
    g_stage_start_ms = now;
  }

  float start_frac = (m.start_state == STEP_DONE) ? 1.0f : 0.0f;
  float discover_frac = (m.discover_state == STEP_DONE) ? 1.0f : 0.0f;
  float ports_frac = (m.ports_state == STEP_DONE || m.ports_state == STEP_SKIPPED) ? 1.0f : 0.0f;
  float finish_frac = (m.finish_state == STEP_DONE) ? 1.0f : 0.0f;

  if (m.start_state == STEP_ACTIVE) {
    unsigned long total_ms = SCAN_WIFI_CONNECT_TIMEOUT_MS + (mode_has_ping ? (SCAN_BROADCAST_WAIT_MS + 1200UL) : 0UL);
    if (total_ms == 0) total_ms = 1000UL;
    float v = (float)(now - g_stage_start_ms) / (float)total_ms;
    start_frac = clamp01(v * 0.95f);
  }

  if (m.discover_state == STEP_ACTIVE) {
    if (s.total > 0) discover_frac = clamp01((float)s.done / (float)s.total);
    else {
      float v = (float)(now - g_stage_start_ms) / (float)SCAN_ARP_TOTAL_WAIT_MS;
      discover_frac = clamp01(v * 0.95f);
    }
  }

  if (m.ports_state == STEP_ACTIVE) {
    if (s.total > 0) ports_frac = clamp01((float)s.done / (float)s.total);
    else {
      float v = (float)(now - g_stage_start_ms) / 45000.0f;
      ports_frac = clamp01(v * 0.95f);
    }
  }

  if (m.finish_state == STEP_ACTIVE) {
    finish_frac = 0.5f;
  }

  int pct = (int)(start_frac * 25.0f + discover_frac * 30.0f + ports_frac * 35.0f + finish_frac * 10.0f + 0.5f);
  if (!s.running && !finished && !error) pct = 0;
  if (finished && !error) pct = 100;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  m.error = error;
  m.finished = finished;
  m.progress_percent = (uint8_t)pct;

  if (error) m.short_status = "ERROR";
  else if (finished) m.short_status = "DONE";
  else if (s.running) m.short_status = "RUNNING";
  else m.short_status = "IDLE";

  return m;
}

void create_gui() {
  Preferences prefs;
  prefs.begin("scanner", true);
  g_theme_light = prefs.getBool("theme_light", false);
  bool ports_legacy = prefs.getBool("ports_legacy", true);
  prefs.end();

  if (ports_legacy) {
    set_port_profile_legacy();
  } else {
    set_port_profile_full();
  }

  g_page_main = lv_obj_create(NULL);
  lv_obj_set_size(g_page_main, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(g_page_main, 0, 0);
  set_bg_flat_style(g_page_main, 0);
  lv_obj_clear_flag(g_page_main, LV_OBJ_FLAG_SCROLLABLE);

  g_lbl_title = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_title, "ESP32S3");
  lv_obj_set_style_text_color(g_lbl_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_title, FONT_TITLE, 0);
  lv_obj_set_width(g_lbl_title, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(g_lbl_title, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(g_lbl_title, LV_ALIGN_TOP_LEFT, 26, 16);

  g_lbl_sd_icon_main = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_sd_icon_main, LV_SYMBOL_SD_CARD);
  lv_obj_set_style_text_font(g_lbl_sd_icon_main, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_sd_icon_main, g_lbl_title, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

  g_lbl_wifi_icon_main = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_wifi_icon_main, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(g_lbl_wifi_icon_main, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_wifi_icon_main, g_lbl_sd_icon_main, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  g_lbl_top_ip_main = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_top_ip_main, "STA -");
  lv_obj_set_style_text_color(g_lbl_top_ip_main, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_top_ip_main, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_top_ip_main, 130);
  lv_label_set_long_mode(g_lbl_top_ip_main, LV_LABEL_LONG_DOT);
  lv_obj_align_to(g_lbl_top_ip_main, g_lbl_wifi_icon_main, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

  g_lbl_ap_icon_main = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_ap_icon_main, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(g_lbl_ap_icon_main, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_ap_icon_main, g_lbl_top_ip_main, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

  g_lbl_top_ap_ip_main = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_top_ap_ip_main, "AP -");
  lv_obj_set_style_text_color(g_lbl_top_ap_ip_main, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_top_ap_ip_main, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_top_ap_ip_main, 120);
  lv_label_set_long_mode(g_lbl_top_ap_ip_main, LV_LABEL_LONG_DOT);
  lv_obj_align_to(g_lbl_top_ap_ip_main, g_lbl_ap_icon_main, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

  g_lbl_hint_main = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_hint_main, "");
  lv_obj_set_style_text_color(g_lbl_hint_main, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(g_lbl_hint_main, FONT_BODY, 0);
  lv_obj_set_style_bg_color(g_lbl_hint_main, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_lbl_hint_main, LV_OPA_COVER, 0);
  lv_obj_set_width(g_lbl_hint_main, LV_SIZE_CONTENT);
  lv_obj_align(g_lbl_hint_main, LV_ALIGN_TOP_RIGHT, -24, 22);

  g_lbl_status = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_status, "STATUS: IDLE");
  lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_status, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_status, STATUS_RIGHT_W);
  lv_label_set_long_mode(g_lbl_status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(g_lbl_status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(g_lbl_status, LV_ALIGN_TOP_RIGHT, -24, 60);

  const int32_t card_y = 124;
  const int32_t gap = 18;
  const int32_t first_x = (LCD_WIDTH - (4 * 220 + 3 * gap)) / 2;
  g_card_start = create_step_card(g_page_main, first_x, card_y, "START", &g_lbl_start_state);
  g_card_discover = create_step_card(g_page_main, first_x + 220 + gap, card_y, "DISCOVER", &g_lbl_discover_state);
  g_card_ports = create_step_card(g_page_main, first_x + 2 * (220 + gap), card_y, "PORTS", &g_lbl_ports_state);
  g_card_finish = create_step_card(g_page_main, first_x + 3 * (220 + gap), card_y, "FINISH", &g_lbl_finish_state);

  g_panel_progress = lv_obj_create(g_page_main);
  lv_obj_set_size(g_panel_progress, LCD_WIDTH - 64, 140);
  lv_obj_align(g_panel_progress, LV_ALIGN_TOP_MID, 0, 306);
  set_transparent_flat_style(g_panel_progress, 12);
  lv_obj_clear_flag(g_panel_progress, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_progress_title = lv_label_create(g_panel_progress);
  lv_label_set_text(lbl_progress_title, "INSTALL PROGRESS");
  lv_obj_set_style_text_color(lbl_progress_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_progress_title, FONT_BIG, 0);
  lv_obj_set_pos(lbl_progress_title, 8, 6);

  g_bar_strip = lv_obj_create(g_panel_progress);
  lv_obj_set_size(g_bar_strip, 790, 28);
  lv_obj_set_pos(g_bar_strip, 8, 50);
  set_transparent_flat_style(g_bar_strip, 0);
  lv_obj_clear_flag(g_bar_strip, LV_OBJ_FLAG_SCROLLABLE);

  const int strip_w = 790;
  const int gap_px = 2;
  const int usable = strip_w - ((int)INSTALL_BLOCKS + 1) * gap_px;
  const int block_h = 22;
  const int base_w = usable / (int)INSTALL_BLOCKS;
  const int remainder = usable - base_w * (int)INSTALL_BLOCKS;
  int x = gap_px;
  for (uint32_t i = 0; i < INSTALL_BLOCKS; i++) {
    int w = base_w + (i < (uint32_t)remainder ? 1 : 0);
    lv_obj_t *b = lv_obj_create(g_bar_strip);
    lv_obj_set_size(b, w, block_h);
    lv_obj_set_pos(b, x, 3);
    set_transparent_flat_style(b, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    g_install_blocks[i] = b;
    x += w + gap_px;
  }

  g_lbl_progress = lv_label_create(g_panel_progress);
  lv_label_set_text(g_lbl_progress, "0%");
  lv_obj_set_style_text_color(g_lbl_progress, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_progress, FONT_BIG, 0);
  lv_obj_align(g_lbl_progress, LV_ALIGN_TOP_RIGHT, -10, 55);

  g_btn_ports_legacy = lv_btn_create(g_page_main);
  lv_obj_set_size(g_btn_ports_legacy, 170, 44);
  lv_obj_align(g_btn_ports_legacy, LV_ALIGN_BOTTOM_LEFT, 30, -84);
  set_transparent_flat_style(g_btn_ports_legacy, 0);
  lv_obj_add_event_cb(g_btn_ports_legacy, ev_set_ports_legacy, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_legacy = lv_label_create(g_btn_ports_legacy);
  lv_label_set_text(lbl_legacy, "LIMITED");
  lv_obj_set_style_text_color(lbl_legacy, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_legacy, FONT_BIG, 0);
  lv_obj_center(lbl_legacy);

  g_btn_ports_full = lv_btn_create(g_page_main);
  lv_obj_set_size(g_btn_ports_full, 170, 44);
  lv_obj_align(g_btn_ports_full, LV_ALIGN_BOTTOM_LEFT, 30, -30);
  set_transparent_flat_style(g_btn_ports_full, 0);
  lv_obj_add_event_cb(g_btn_ports_full, ev_set_ports_full, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_full = lv_label_create(g_btn_ports_full);
  lv_label_set_text(lbl_full, "FULL");
  lv_obj_set_style_text_color(lbl_full, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_full, FONT_BIG, 0);
  lv_obj_center(lbl_full);

  g_btn_nav_ports = lv_btn_create(g_page_main);
  lv_obj_set_size(g_btn_nav_ports, 170, 44);
  lv_obj_align(g_btn_nav_ports, LV_ALIGN_BOTTOM_RIGHT, -30, -84);
  set_transparent_flat_style(g_btn_nav_ports, 0);
  lv_obj_add_event_cb(g_btn_nav_ports, ev_nav_ports, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_nav_ports = lv_label_create(g_btn_nav_ports);
  lv_label_set_text(lbl_nav_ports, "LIST");
  lv_obj_set_style_text_color(lbl_nav_ports, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_nav_ports, FONT_BIG, 0);
  lv_obj_center(lbl_nav_ports);

  g_btn_nav_log = lv_btn_create(g_page_main);
  lv_obj_set_size(g_btn_nav_log, 170, 44);
  lv_obj_align(g_btn_nav_log, LV_ALIGN_BOTTOM_RIGHT, -30, -30);
  set_transparent_flat_style(g_btn_nav_log, 0);
  lv_obj_add_event_cb(g_btn_nav_log, ev_nav_log, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_nav_log = lv_label_create(g_btn_nav_log);
  lv_label_set_text(lbl_nav_log, "LOG");
  lv_obj_set_style_text_color(lbl_nav_log, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_nav_log, FONT_BIG, 0);
  lv_obj_center(lbl_nav_log);

  g_btn_start = lv_btn_create(g_page_main);
  lv_obj_set_size(g_btn_start, 220, 52);
  lv_obj_align(g_btn_start, LV_ALIGN_BOTTOM_LEFT, 30, -18);
  set_transparent_flat_style(g_btn_start, 0);
  lv_obj_add_event_cb(g_btn_start, ev_start_scan, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_local = lv_label_create(g_btn_start);
  lv_label_set_text(lbl_local, "START");
  lv_obj_set_style_text_color(lbl_local, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_local, FONT_BIG, 0);
  lv_obj_center(lbl_local);

  lv_obj_align(g_btn_start, LV_ALIGN_BOTTOM_MID, 0, -18);

  g_page_log = lv_obj_create(NULL);
  lv_obj_set_size(g_page_log, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(g_page_log, 0, 0);
  set_bg_flat_style(g_page_log, 0);
  lv_obj_clear_flag(g_page_log, LV_OBJ_FLAG_SCROLLABLE);

  g_lbl_log_title = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_log_title, "LIVE LOG");
  lv_obj_set_style_text_color(g_lbl_log_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_log_title, FONT_TITLE, 0);
  lv_obj_set_width(g_lbl_log_title, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(g_lbl_log_title, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(g_lbl_log_title, LV_ALIGN_TOP_LEFT, 24, 16);

  g_lbl_sd_icon_log = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_sd_icon_log, LV_SYMBOL_SD_CARD);
  lv_obj_set_style_text_font(g_lbl_sd_icon_log, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_sd_icon_log, g_lbl_log_title, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

  g_lbl_wifi_icon_log = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_wifi_icon_log, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(g_lbl_wifi_icon_log, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_wifi_icon_log, g_lbl_sd_icon_log, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  g_lbl_top_ip_log = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_top_ip_log, "STA -");
  lv_obj_set_style_text_color(g_lbl_top_ip_log, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_top_ip_log, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_top_ip_log, 130);
  lv_label_set_long_mode(g_lbl_top_ip_log, LV_LABEL_LONG_DOT);
  lv_obj_align_to(g_lbl_top_ip_log, g_lbl_wifi_icon_log, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

  g_lbl_ap_icon_log = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_ap_icon_log, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(g_lbl_ap_icon_log, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_ap_icon_log, g_lbl_top_ip_log, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

  g_lbl_top_ap_ip_log = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_top_ap_ip_log, "AP -");
  lv_obj_set_style_text_color(g_lbl_top_ap_ip_log, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_top_ap_ip_log, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_top_ap_ip_log, 120);
  lv_label_set_long_mode(g_lbl_top_ap_ip_log, LV_LABEL_LONG_DOT);
  lv_obj_align_to(g_lbl_top_ap_ip_log, g_lbl_ap_icon_log, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

  g_lbl_hint_log = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_hint_log, "");
  lv_obj_set_style_text_color(g_lbl_hint_log, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(g_lbl_hint_log, FONT_BODY, 0);
  lv_obj_set_style_bg_color(g_lbl_hint_log, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_lbl_hint_log, LV_OPA_COVER, 0);
  lv_obj_set_width(g_lbl_hint_log, LV_SIZE_CONTENT);
  lv_obj_align(g_lbl_hint_log, LV_ALIGN_TOP_RIGHT, -24, 22);

  g_lbl_log_status = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_log_status, "STATUS: IDLE  0/0");
  lv_obj_set_style_text_color(g_lbl_log_status, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_log_status, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_log_status, STATUS_RIGHT_W);
  lv_label_set_long_mode(g_lbl_log_status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(g_lbl_log_status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(g_lbl_log_status, LV_ALIGN_TOP_RIGHT, -24, 60);

  g_panel_log = lv_obj_create(g_page_log);
  lv_obj_set_size(g_panel_log, LCD_WIDTH - 240, 440);
  lv_obj_align(g_panel_log, LV_ALIGN_TOP_LEFT, 24, 84);
  set_transparent_flat_style(g_panel_log, 10);
  lv_obj_set_scrollbar_mode(g_panel_log, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *lbl_log_caption = lv_label_create(g_panel_log);
  lv_label_set_text(lbl_log_caption, "EVENTS");
  lv_obj_set_style_text_color(lbl_log_caption, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_log_caption, FONT_BIG, 0);
  lv_obj_align(lbl_log_caption, LV_ALIGN_TOP_LEFT, 0, 0);

  g_lbl_log = lv_label_create(g_panel_log);
  lv_obj_set_width(g_lbl_log, LCD_WIDTH - 272);
  lv_label_set_long_mode(g_lbl_log, LV_LABEL_LONG_WRAP);
  lv_label_set_text(g_lbl_log, "Waiting for logs...\n");
  lv_obj_set_style_text_color(g_lbl_log, lv_color_hex(0x60A5FA), 0);
  lv_obj_set_style_text_font(g_lbl_log, FONT_BODY, 0);
  lv_obj_align(g_lbl_log, LV_ALIGN_TOP_LEFT, 0, 28);

  g_btn_theme_dark = lv_btn_create(g_page_log);
  lv_obj_set_size(g_btn_theme_dark, 170, 44);
  lv_obj_align(g_btn_theme_dark, LV_ALIGN_TOP_RIGHT, -30, 84);
  set_transparent_flat_style(g_btn_theme_dark, 0);
  lv_obj_add_event_cb(g_btn_theme_dark, ev_set_theme_dark, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_dark = lv_label_create(g_btn_theme_dark);
  lv_label_set_text(lbl_dark, "DARK");
  lv_obj_set_style_text_font(lbl_dark, FONT_BIG, 0);
  lv_obj_center(lbl_dark);

  g_btn_theme_light = lv_btn_create(g_page_log);
  lv_obj_set_size(g_btn_theme_light, 170, 44);
  lv_obj_align(g_btn_theme_light, LV_ALIGN_TOP_RIGHT, -30, 144);
  set_transparent_flat_style(g_btn_theme_light, 0);
  lv_obj_add_event_cb(g_btn_theme_light, ev_set_theme_light, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_light = lv_label_create(g_btn_theme_light);
  lv_label_set_text(lbl_light, "LIGHT");
  lv_obj_set_style_text_font(lbl_light, FONT_BIG, 0);
  lv_obj_center(lbl_light);

  g_btn_sd_ota = lv_btn_create(g_page_log);
  lv_obj_set_size(g_btn_sd_ota, 170, 44);
  lv_obj_align(g_btn_sd_ota, LV_ALIGN_TOP_RIGHT, -30, 204);
  set_transparent_flat_style(g_btn_sd_ota, 0);
  lv_obj_add_event_cb(g_btn_sd_ota, ev_sd_ota, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_sd_ota = lv_label_create(g_btn_sd_ota);
  lv_label_set_text(lbl_sd_ota, "SD OTA");
  lv_obj_set_style_text_font(lbl_sd_ota, FONT_BIG, 0);
  lv_obj_center(lbl_sd_ota);

  g_btn_ap_toggle = lv_btn_create(g_page_log);
  lv_obj_set_size(g_btn_ap_toggle, 170, 44);
  lv_obj_align(g_btn_ap_toggle, LV_ALIGN_TOP_RIGHT, -30, 264);
  set_transparent_flat_style(g_btn_ap_toggle, 0);
  lv_obj_add_event_cb(g_btn_ap_toggle, ev_ap_toggle, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_ap_toggle = lv_label_create(g_btn_ap_toggle);
  lv_label_set_text(lbl_ap_toggle, "AP ON/OFF");
  lv_obj_set_style_text_font(lbl_ap_toggle, FONT_BIG, 0);
  lv_obj_center(lbl_ap_toggle);

  g_btn_sd_wipe = lv_btn_create(g_page_log);
  lv_obj_set_size(g_btn_sd_wipe, 170, 44);
  lv_obj_align(g_btn_sd_wipe, LV_ALIGN_TOP_RIGHT, -30, 324);
  set_transparent_flat_style(g_btn_sd_wipe, 0);
  lv_obj_add_event_cb(g_btn_sd_wipe, ev_sd_wipe, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_sd_wipe = lv_label_create(g_btn_sd_wipe);
  lv_label_set_text(lbl_sd_wipe, "WIPE SD");
  lv_obj_set_style_text_font(lbl_sd_wipe, FONT_BIG, 0);
  lv_obj_center(lbl_sd_wipe);

  g_btn_back_log = lv_btn_create(g_page_log);
  lv_obj_set_size(g_btn_back_log, 170, 44);
  lv_obj_align(g_btn_back_log, LV_ALIGN_BOTTOM_RIGHT, -30, -30);
  set_transparent_flat_style(g_btn_back_log, 0);
  lv_obj_add_event_cb(g_btn_back_log, ev_nav_main, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_back_log = lv_label_create(g_btn_back_log);
  lv_label_set_text(lbl_back_log, "BACK");
  lv_obj_set_style_text_color(lbl_back_log, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_back_log, FONT_BIG, 0);
  lv_obj_center(lbl_back_log);

  g_page_ports = lv_obj_create(NULL);
  lv_obj_set_size(g_page_ports, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(g_page_ports, 0, 0);
  set_bg_flat_style(g_page_ports, 0);
  lv_obj_clear_flag(g_page_ports, LV_OBJ_FLAG_SCROLLABLE);

  g_lbl_ports_title = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_ports_title, "HOSTS AND OPEN PORTS");
  lv_obj_set_style_text_color(g_lbl_ports_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_ports_title, FONT_TITLE, 0);
  lv_obj_set_width(g_lbl_ports_title, LV_SIZE_CONTENT);
  lv_obj_set_style_text_align(g_lbl_ports_title, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(g_lbl_ports_title, LV_ALIGN_TOP_LEFT, 24, 16);

  g_lbl_sd_icon_ports = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_sd_icon_ports, LV_SYMBOL_SD_CARD);
  lv_obj_set_style_text_font(g_lbl_sd_icon_ports, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_sd_icon_ports, g_lbl_ports_title, LV_ALIGN_OUT_RIGHT_MID, 20, 0);

  g_lbl_wifi_icon_ports = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_wifi_icon_ports, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(g_lbl_wifi_icon_ports, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_wifi_icon_ports, g_lbl_sd_icon_ports, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  g_lbl_top_ip_ports = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_top_ip_ports, "STA -");
  lv_obj_set_style_text_color(g_lbl_top_ip_ports, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_top_ip_ports, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_top_ip_ports, 130);
  lv_label_set_long_mode(g_lbl_top_ip_ports, LV_LABEL_LONG_DOT);
  lv_obj_align_to(g_lbl_top_ip_ports, g_lbl_wifi_icon_ports, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

  g_lbl_ap_icon_ports = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_ap_icon_ports, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(g_lbl_ap_icon_ports, FONT_TITLE, 0);
  lv_obj_align_to(g_lbl_ap_icon_ports, g_lbl_top_ip_ports, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

  g_lbl_top_ap_ip_ports = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_top_ap_ip_ports, "AP -");
  lv_obj_set_style_text_color(g_lbl_top_ap_ip_ports, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_top_ap_ip_ports, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_top_ap_ip_ports, 120);
  lv_label_set_long_mode(g_lbl_top_ap_ip_ports, LV_LABEL_LONG_DOT);
  lv_obj_align_to(g_lbl_top_ap_ip_ports, g_lbl_ap_icon_ports, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

  g_lbl_hint_ports = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_hint_ports, "");
  lv_obj_set_style_text_color(g_lbl_hint_ports, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(g_lbl_hint_ports, FONT_BODY, 0);
  lv_obj_set_style_bg_color(g_lbl_hint_ports, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_lbl_hint_ports, LV_OPA_COVER, 0);
  lv_obj_set_width(g_lbl_hint_ports, LV_SIZE_CONTENT);
  lv_obj_align(g_lbl_hint_ports, LV_ALIGN_TOP_RIGHT, -24, 22);

  g_lbl_ports_status = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_ports_status, "STATUS: IDLE  0/0");
  lv_obj_set_style_text_color(g_lbl_ports_status, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_ports_status, FONT_BODY, 0);
  lv_obj_set_width(g_lbl_ports_status, STATUS_RIGHT_W);
  lv_label_set_long_mode(g_lbl_ports_status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(g_lbl_ports_status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(g_lbl_ports_status, LV_ALIGN_TOP_RIGHT, -24, 60);

  g_panel_ports = lv_obj_create(g_page_ports);
  lv_obj_set_size(g_panel_ports, LCD_WIDTH - 48, 440);
  lv_obj_align(g_panel_ports, LV_ALIGN_TOP_MID, 0, 84);
  set_transparent_flat_style(g_panel_ports, 10);
  lv_obj_set_scrollbar_mode(g_panel_ports, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *lbl_ports_caption = lv_label_create(g_panel_ports);
  lv_label_set_text(lbl_ports_caption, "IP -> OPEN PORTS");
  lv_obj_set_style_text_color(lbl_ports_caption, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_ports_caption, FONT_BIG, 0);
  lv_obj_align(lbl_ports_caption, LV_ALIGN_TOP_LEFT, 0, 0);

  g_lbl_open_ports = lv_label_create(g_panel_ports);
  lv_obj_set_width(g_lbl_open_ports, LCD_WIDTH - 80);
  lv_label_set_long_mode(g_lbl_open_ports, LV_LABEL_LONG_WRAP);
  lv_label_set_text(g_lbl_open_ports, "No hosts with open ports.");
  lv_obj_set_style_text_color(g_lbl_open_ports, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_open_ports, FONT_BODY, 0);
  lv_label_set_recolor(g_lbl_open_ports, true);
  lv_obj_align(g_lbl_open_ports, LV_ALIGN_TOP_LEFT, 0, 28);

  g_btn_back_ports = lv_btn_create(g_page_ports);
  lv_obj_set_size(g_btn_back_ports, 170, 44);
  lv_obj_align(g_btn_back_ports, LV_ALIGN_BOTTOM_RIGHT, -30, -30);
  set_transparent_flat_style(g_btn_back_ports, 0);
  lv_obj_add_event_cb(g_btn_back_ports, ev_nav_main, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_back_ports = lv_label_create(g_btn_back_ports);
  lv_label_set_text(lbl_back_ports, "BACK");
  lv_obj_set_style_text_color(lbl_back_ports, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_back_ports, FONT_BIG, 0);
  lv_obj_center(lbl_back_ports);

  set_page(PAGE_MAIN, LV_SCR_LOAD_ANIM_NONE);
  set_install_bar_percent(0);

  AppSnapshot init_snapshot;
  snapshot_state(init_snapshot);
  ScanUiModel init_model = compute_ui_model(init_snapshot);
  set_step_card_state(g_card_start, g_lbl_start_state, init_model.start_state, false);
  set_step_card_state(g_card_discover, g_lbl_discover_state, init_model.discover_state, false);
  set_step_card_state(g_card_ports, g_lbl_ports_state, init_model.ports_state, false);
  set_step_card_state(g_card_finish, g_lbl_finish_state, init_model.finish_state, init_model.error);
  set_install_bar_percent(init_model.progress_percent);
  apply_theme();
}

static void show_sd_ota_prompt(bool allow_update) {
  if (g_ota_prompt) return;

  g_ota_prompt = lv_obj_create(lv_layer_top());
  lv_obj_set_size(g_ota_prompt, 500, 250);
  lv_obj_center(g_ota_prompt);
  lv_obj_set_style_bg_color(g_ota_prompt, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(g_ota_prompt, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_ota_prompt, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_border_width(g_ota_prompt, 2, 0);
  lv_obj_set_style_radius(g_ota_prompt, 6, 0);
  lv_obj_set_style_pad_all(g_ota_prompt, 18, 0);
  lv_obj_clear_flag(g_ota_prompt, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(g_ota_prompt);
  lv_label_set_text(title, "SD UPDATE");
  lv_obj_set_style_text_color(title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(title, FONT_BIG, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  String info = allow_update ? String("File found:\n") + sd_ota_file_info() + "\n\nUpdate firmware now?"
                             : String("File not found:\n") + sd_ota_file_info();
  lv_obj_t *msg = lv_label_create(g_ota_prompt);
  lv_label_set_text(msg, info.c_str());
  lv_obj_set_style_text_color(msg, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(msg, FONT_BODY, 0);
  lv_obj_set_width(msg, 450);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 0, 40);

  if (allow_update) {
    lv_obj_t *btn_no = lv_btn_create(g_ota_prompt);
    lv_obj_set_size(btn_no, 150, 48);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_LEFT, 12, -4);
    lv_obj_add_event_cb(btn_no, ev_ota_no, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, "NO");
    lv_obj_set_style_text_font(lbl_no, FONT_BIG, 0);
    lv_obj_center(lbl_no);

    lv_obj_t *btn_yes = lv_btn_create(g_ota_prompt);
    lv_obj_set_size(btn_yes, 150, 48);
    lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_RIGHT, -12, -4);
    lv_obj_add_event_cb(btn_yes, ev_ota_yes, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_yes, "YES");
    lv_obj_set_style_text_font(lbl_yes, FONT_BIG, 0);
    lv_obj_center(lbl_yes);
    append_log(String("OTA: found ") + sd_ota_file_path());
  } else {
    lv_obj_t *btn_ok = lv_btn_create(g_ota_prompt);
    lv_obj_set_size(btn_ok, 150, 48);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_event_cb(btn_ok, ev_ota_ok, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, "OK");
    lv_obj_set_style_text_font(lbl_ok, FONT_BIG, 0);
    lv_obj_center(lbl_ok);
  }
}

static void show_sd_ota_status_prompt() {
  show_sd_ota_prompt(sd_ota_file_available());
}

void show_sd_ota_prompt_if_available() {
  if (!sd_ota_file_available()) return;
  show_sd_ota_prompt(true);
}

void refresh_ui() {
  enforce_theme_transparent_visuals();

  AppSnapshot s;
  snapshot_state(s);
  ScanUiModel m = compute_ui_model(s);

  bool wifi_is_ok = (WiFi.status() == WL_CONNECTED);
  bool ap_is_ok = wifi_portal_running();
  bool sd_is_ok = scan_log_sd_ready();

  if (g_ui_last_wifi_ok != (wifi_is_ok ? 1 : 0)) {
    g_ui_last_wifi_ok = wifi_is_ok ? 1 : 0;
    lv_color_t color = wifi_is_ok ? lv_color_hex(0x22C55E) : lv_color_hex(0xEF4444);
    if (g_lbl_wifi_icon_main) lv_obj_set_style_text_color(g_lbl_wifi_icon_main, color, 0);
    if (g_lbl_wifi_icon_log) lv_obj_set_style_text_color(g_lbl_wifi_icon_log, color, 0);
    if (g_lbl_wifi_icon_ports) lv_obj_set_style_text_color(g_lbl_wifi_icon_ports, color, 0);
    if (g_lbl_top_ip_main) lv_obj_set_style_text_color(g_lbl_top_ip_main, wifi_is_ok ? color : get_theme_text_muted(), 0);
    if (g_lbl_top_ip_log) lv_obj_set_style_text_color(g_lbl_top_ip_log, wifi_is_ok ? color : get_theme_text_muted(), 0);
    if (g_lbl_top_ip_ports) lv_obj_set_style_text_color(g_lbl_top_ip_ports, wifi_is_ok ? color : get_theme_text_muted(), 0);
  }

  if (g_ui_last_ap_ok != (ap_is_ok ? 1 : 0)) {
    g_ui_last_ap_ok = ap_is_ok ? 1 : 0;
    lv_color_t color = ap_is_ok ? lv_color_hex(0x38BDF8) : lv_color_hex(0x64748B);
    if (g_lbl_ap_icon_main) lv_obj_set_style_text_color(g_lbl_ap_icon_main, color, 0);
    if (g_lbl_ap_icon_log) lv_obj_set_style_text_color(g_lbl_ap_icon_log, color, 0);
    if (g_lbl_ap_icon_ports) lv_obj_set_style_text_color(g_lbl_ap_icon_ports, color, 0);
    if (g_lbl_top_ap_ip_main) lv_obj_set_style_text_color(g_lbl_top_ap_ip_main, color, 0);
    if (g_lbl_top_ap_ip_log) lv_obj_set_style_text_color(g_lbl_top_ap_ip_log, color, 0);
    if (g_lbl_top_ap_ip_ports) lv_obj_set_style_text_color(g_lbl_top_ap_ip_ports, color, 0);
    if (g_btn_ap_toggle) {
      lv_obj_t *lbl = lv_obj_get_child(g_btn_ap_toggle, 0);
      if (lbl) lv_label_set_text(lbl, ap_is_ok ? "AP OFF" : "AP ON");
    }
  }

  static lv_color_t current_anim_color = lv_color_hex(0xFFFFFF);
  static uint32_t last_color_change = 0;
  static uint16_t cur_h = 0;
  static uint16_t target_h = 120;
  if (millis() - last_color_change > 30) {
      last_color_change = millis();
      int16_t diff = target_h - cur_h;
      if (diff > 180) diff -= 360;
      if (diff < -180) diff += 360;
      if (abs(diff) <= 1) {
          target_h = random(0, 360);
      } else {
          cur_h = (cur_h + (diff > 0 ? 1 : -1) + 360) % 360;
      }
      current_anim_color = lv_color_hsv_to_rgb(cur_h, 80, 100);
  }

  if (g_ui_last_sd_ok != (sd_is_ok ? 1 : 0)) {
    g_ui_last_sd_ok = sd_is_ok ? 1 : 0;
    lv_color_t color = sd_is_ok ? lv_color_hex(0x22C55E) : lv_color_hex(0xEF4444);
    if (g_lbl_sd_icon_main) lv_obj_set_style_text_color(g_lbl_sd_icon_main, color, 0);
    if (g_lbl_sd_icon_log) lv_obj_set_style_text_color(g_lbl_sd_icon_log, color, 0);
    if (g_lbl_sd_icon_ports) lv_obj_set_style_text_color(g_lbl_sd_icon_ports, color, 0);
  }

  String ip_line = "STA -";
  if (s.wifi.startsWith("WiFi: ") && !s.wifi.startsWith("WiFi: OFF") && !s.wifi.startsWith("WiFi: connecting")) {
      ip_line = String("STA ") + s.wifi.substring(6);
  }
  if (ip_line != g_ui_last_wifi) {
      g_ui_last_wifi = ip_line;
      if (g_lbl_top_ip_main) lv_label_set_text(g_lbl_top_ip_main, ip_line.c_str());
      if (g_lbl_top_ip_log) lv_label_set_text(g_lbl_top_ip_log, ip_line.c_str());
      if (g_lbl_top_ip_ports) lv_label_set_text(g_lbl_top_ip_ports, ip_line.c_str());
  }

  String ap_ip_line = ap_is_ok ? String("AP ") + WiFi.softAPIP().toString() : "AP -";
  if (ap_ip_line != g_ui_last_ap_ip) {
      g_ui_last_ap_ip = ap_ip_line;
      if (g_lbl_top_ap_ip_main) lv_label_set_text(g_lbl_top_ap_ip_main, ap_ip_line.c_str());
      if (g_lbl_top_ap_ip_log) lv_label_set_text(g_lbl_top_ap_ip_log, ap_ip_line.c_str());
      if (g_lbl_top_ap_ip_ports) lv_label_set_text(g_lbl_top_ap_ip_ports, ap_ip_line.c_str());
  }

  if (s.status != g_ui_last_status || (int)m.progress_percent != g_ui_last_percent ||
      s.done != g_ui_last_done || s.total != g_ui_last_total) {
    g_ui_last_status = s.status;
    g_ui_last_percent = (int)m.progress_percent;
    g_ui_last_done = s.done;
    g_ui_last_total = s.total;

    String short_state = String("STATUS: ") + m.short_status;
    if (g_lbl_status) lv_label_set_text(g_lbl_status, short_state.c_str());

    String log_state = short_state + "  " + String(s.done) + "/" + String(s.total);
    if (g_lbl_log_status) lv_label_set_text(g_lbl_log_status, log_state.c_str());
    if (g_lbl_ports_status) lv_label_set_text(g_lbl_ports_status, log_state.c_str());
  }

  update_port_profile_buttons();

  if (g_ui_last_start_state != (int)m.start_state) {
    g_ui_last_start_state = (int)m.start_state;
    set_step_card_state(g_card_start, g_lbl_start_state, m.start_state, false);
  }
  if (g_ui_last_discover_state != (int)m.discover_state) {
    g_ui_last_discover_state = (int)m.discover_state;
    set_step_card_state(g_card_discover, g_lbl_discover_state, m.discover_state, false);
  }
  if (g_ui_last_ports_state != (int)m.ports_state) {
    g_ui_last_ports_state = (int)m.ports_state;
    set_step_card_state(g_card_ports, g_lbl_ports_state, m.ports_state, false);
  }
  if (g_ui_last_finish_state != (int)m.finish_state || g_ui_last_finish_error != (m.error ? 1 : 0)) {
    g_ui_last_finish_state = (int)m.finish_state;
    g_ui_last_finish_error = m.error ? 1 : 0;
    set_step_card_state(g_card_finish, g_lbl_finish_state, m.finish_state, m.error);
  }
  if (g_ui_last_bar_percent != (int)m.progress_percent) {
    g_ui_last_bar_percent = (int)m.progress_percent;
    set_install_bar_percent(m.progress_percent);
  }

  static unsigned long last_log_refresh = 0;
  unsigned long now = millis();
  if (s.log != g_ui_last_log && (now - last_log_refresh) >= 120) {
    last_log_refresh = now;
    g_ui_last_log = s.log;

    if (g_lbl_log) {
      String recent = extract_recent_lines(s.log, 60);
      if (!recent.length()) recent = "Waiting for logs...\n";
      lv_label_set_text(g_lbl_log, recent.c_str());
    }
    if (g_panel_log) {
      lv_obj_scroll_to_y(g_panel_log, LV_COORD_MAX, LV_ANIM_OFF);
    }

    String open_ports = collect_open_ports_by_ip(s.log);
    if (open_ports != g_ui_last_open_ports && g_lbl_open_ports) {
      g_ui_last_open_ports = open_ports;
      if (!open_ports.length()) {
        lv_label_set_text(g_lbl_open_ports, "No hosts with open ports.");
      } else {
        int cnt = count_lines(open_ports);
        String txt = String("Hosts with open ports: ") + cnt + "\n\n" + open_ports;
        lv_label_set_text(g_lbl_open_ports, txt.c_str());
      }
    }
  }

  if (g_btn_start) {
    lv_obj_t *lbl = lv_obj_get_child(g_btn_start, 0);
    if (s.running) {
      lv_obj_add_state(g_btn_start, LV_STATE_DISABLED);
      if (lbl) {
        if ((millis() / 150) % 2 == 0) {
          lv_obj_set_style_text_color(lbl, current_anim_color, 0);
        } else {
          lv_obj_set_style_text_color(lbl, get_theme_bg(), 0);
        }
      }
    } else {
      lv_obj_clear_state(g_btn_start, LV_STATE_DISABLED);
      if (lbl) lv_obj_set_style_text_color(lbl, get_theme_text(), 0);
    }
  }

  static unsigned long last_wifi_refresh = 0;
  if ((now - last_wifi_refresh) > 1200) {
    last_wifi_refresh = now;
    if (WiFi.status() == WL_CONNECTED) set_wifi_text(String("WiFi: ") + WiFi.localIP().toString());
    else if (s.running) set_wifi_text("WiFi: connecting...");
  }

  static unsigned long last_time_refresh = 0;
  static bool ntp_configured = false;
  static String last_time_str = "";
  if ((now - last_time_refresh) > 1000) {
    last_time_refresh = now;

    if (WiFi.status() == WL_CONNECTED && !ntp_configured) {
      configTzTime(SCAN_LOG_TZ, SCAN_LOG_NTP_SERVER_1, SCAN_LOG_NTP_SERVER_2, SCAN_LOG_NTP_SERVER_3);
      ntp_configured = true;
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
      char time_buf[64];
      strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      if (last_time_str != time_buf) {
        last_time_str = time_buf;
        if (g_lbl_hint_main) lv_label_set_text(g_lbl_hint_main, time_buf);
        if (g_lbl_hint_log) lv_label_set_text(g_lbl_hint_log, time_buf);
        if (g_lbl_hint_ports) lv_label_set_text(g_lbl_hint_ports, time_buf);
      }
    } else {
      if (last_time_str != "No time (NTP)") {
        last_time_str = "No time (NTP)";
        if (g_lbl_hint_main) lv_label_set_text(g_lbl_hint_main, "No time (NTP)");
        if (g_lbl_hint_log) lv_label_set_text(g_lbl_hint_log, "No time (NTP)");
        if (g_lbl_hint_ports) lv_label_set_text(g_lbl_hint_ports, "No time (NTP)");
      }
    }
  }
}
