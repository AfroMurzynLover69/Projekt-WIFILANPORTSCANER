#include "gui.h"

#include <Arduino.h>
#include <WiFi.h>
#define LV_CONF_PATH "lv_conf.h"
#include <lvgl.h>
#include <vector>

#include "konfiguracja.h"

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

static String g_ui_last_wifi = "";
static String g_ui_last_status = "";
static String g_ui_last_log = "";
static String g_ui_last_ips = "";
static String g_ui_last_open_ports = "";
static int g_ui_last_percent = -1;
static uint32_t g_ui_last_done = UINT32_MAX;
static uint32_t g_ui_last_total = UINT32_MAX;

static lv_obj_t *g_page_main = nullptr;
static lv_obj_t *g_page_log = nullptr;
static lv_obj_t *g_page_ports = nullptr;

static lv_obj_t *g_lbl_title = nullptr;
static lv_obj_t *g_lbl_hint_main = nullptr;
static lv_obj_t *g_lbl_wifi = nullptr;
static lv_obj_t *g_lbl_status = nullptr;
static lv_obj_t *g_lbl_mode = nullptr;

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

static lv_obj_t *g_lbl_log_title = nullptr;
static lv_obj_t *g_lbl_hint_log = nullptr;
static lv_obj_t *g_lbl_log_wifi = nullptr;
static lv_obj_t *g_lbl_log_status = nullptr;
static lv_obj_t *g_panel_log = nullptr;
static lv_obj_t *g_lbl_log = nullptr;
static lv_obj_t *g_panel_hosts = nullptr;
static lv_obj_t *g_lbl_ips = nullptr;

static lv_obj_t *g_lbl_ports_title = nullptr;
static lv_obj_t *g_lbl_hint_ports = nullptr;
static lv_obj_t *g_lbl_ports_wifi = nullptr;
static lv_obj_t *g_lbl_ports_status = nullptr;
static lv_obj_t *g_panel_ports = nullptr;
static lv_obj_t *g_lbl_open_ports = nullptr;

static StageId g_stage_active = STAGE_NONE;
static unsigned long g_stage_start_ms = 0;
static PageId g_current_page = PAGE_MAIN;

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

static String collect_discovered_ips(const String &log) {
  String out = "";
  String seen = "|";

  int pos = 0;
  while (true) {
    int marker = log.indexOf("ARP ", pos);
    if (marker < 0) break;

    int ip_start = marker + 4;
    int ip_end = log.indexOf(" UP", ip_start);
    if (ip_end < 0) {
      pos = ip_start;
      continue;
    }

    String ip = log.substring(ip_start, ip_end);
    if (ip.length() >= 7 && ip.length() <= 15) {
      String key = String("|") + ip + "|";
      if (seen.indexOf(key) < 0) {
        seen += ip;
        seen += "|";
        if (out.length()) out += "\n";
        out += ip;
      }
    }

    pos = ip_end + 3;
  }

  return out;
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
      list_ports[(unsigned int)idx] += port_s;
    }

    pos = payload_end + 5;
  }

  String out = "";
  for (unsigned int i = 0; i < ips.size(); i++) {
    if (out.length()) out += "\n";
    out += ips[i];
    out += " -> ";
    out += list_ports[i].length() ? list_ports[i] : "-";
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

static void set_page(PageId page) {
  if (g_page_main) {
    if (page == PAGE_MAIN) lv_obj_clear_flag(g_page_main, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_page_main, LV_OBJ_FLAG_HIDDEN);
  }

  if (g_page_log) {
    if (page == PAGE_LOG) lv_obj_clear_flag(g_page_log, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_page_log, LV_OBJ_FLAG_HIDDEN);
  }

  if (g_page_ports) {
    if (page == PAGE_PORTS) lv_obj_clear_flag(g_page_ports, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_page_ports, LV_OBJ_FLAG_HIDDEN);
  }

  g_current_page = page;
}

static void ev_horizontal_nav(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;

  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;

  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_RIGHT) {
    if (g_current_page == PAGE_MAIN) set_page(PAGE_LOG);
    else if (g_current_page == PAGE_PORTS) set_page(PAGE_MAIN);
  }
  if (dir == LV_DIR_LEFT) {
    if (g_current_page == PAGE_MAIN) set_page(PAGE_PORTS);
    else if (g_current_page == PAGE_LOG) set_page(PAGE_MAIN);
  }
}

static void ev_start_scan(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  bool ok = start_scan_request("UI: start local scan");
  if (!ok) {
    set_status("Status: scan juz trwa");
    append_log("UI: LOCAL start odrzucony");
  } else {
    set_status("Status: start local scan...");
  }
}

static void update_port_profile_buttons() {
  bool legacy = is_port_profile_legacy();
  if (g_btn_ports_legacy) {
    lv_obj_set_style_bg_color(g_btn_ports_legacy, legacy ? lv_color_hex(0x2563EB) : lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_color(g_btn_ports_legacy, legacy ? lv_color_hex(0x93C5FD) : lv_color_hex(0x334155), 0);
  }
  if (g_btn_ports_full) {
    lv_obj_set_style_bg_color(g_btn_ports_full, legacy ? lv_color_hex(0x0F172A) : lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_border_color(g_btn_ports_full, legacy ? lv_color_hex(0x334155) : lv_color_hex(0x93C5FD), 0);
  }
}

static void ev_set_ports_legacy(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (is_scan_busy()) {
    set_status("Status: zmiana portow zablokowana");
    append_log("UI: nie mozna zmienic portow podczas skanu");
    return;
  }
  set_port_profile_legacy();
  set_status("Status: porty 0-4096");
  append_log("UI: profil portow 0-4096");
  update_port_profile_buttons();
}

static void ev_set_ports_full(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (is_scan_busy()) {
    set_status("Status: zmiana portow zablokowana");
    append_log("UI: nie mozna zmienic portow podczas skanu");
    return;
  }
  set_port_profile_full();
  set_status("Status: porty FULL");
  append_log("UI: profil portow FULL");
  update_port_profile_buttons();
}

static lv_obj_t *create_step_card(lv_obj_t *parent, int32_t x, int32_t y, const char *title, lv_obj_t **state_lbl_out) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 220, 170);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(card, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  lv_obj_t *lbl_title = lv_label_create(card);
  lv_label_set_text(lbl_title, title);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *lbl_state = lv_label_create(card);
  lv_label_set_text(lbl_state, "WAIT");
  lv_obj_set_style_text_color(lbl_state, lv_color_hex(0x93A4B8), 0);
  lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_state, LV_ALIGN_CENTER, 0, 10);

  if (state_lbl_out) *state_lbl_out = lbl_state;
  return card;
}

static void set_step_card_state(lv_obj_t *card, lv_obj_t *state_lbl, StepState state, bool mark_error) {
  if (!card || !state_lbl) return;

  lv_color_t bg = lv_color_hex(0x0F172A);
  lv_color_t text = lv_color_hex(0x93A4B8);
  const char *state_word = "WAIT";

  if (state == STEP_ACTIVE) {
    bg = lv_color_hex(0x123A75);
    text = lv_color_hex(0xFFFFFF);
    state_word = "RUN";
  } else if (state == STEP_DONE) {
    bg = lv_color_hex(0x0F5132);
    text = lv_color_hex(0xFFFFFF);
    state_word = "OK";
  } else if (state == STEP_SKIPPED) {
    bg = lv_color_hex(0x111111);
    text = lv_color_hex(0x9CA3AF);
    state_word = "SKIP";
  }

  if (mark_error && state == STEP_DONE) {
    bg = lv_color_hex(0x5A1D1D);
    text = lv_color_hex(0xFFFFFF);
    state_word = "ERR";
  }

  lv_obj_set_style_bg_color(card, bg, 0);
  lv_label_set_text(state_lbl, state_word);
  lv_obj_set_style_text_color(state_lbl, text, 0);
}

static void set_install_bar_percent(uint8_t pct) {
  for (uint32_t i = 0; i < INSTALL_BLOCKS; i++) {
    if (!g_install_blocks[i]) continue;

    uint32_t threshold = ((i + 1) * 100U + INSTALL_BLOCKS - 1U) / INSTALL_BLOCKS;
    bool on = pct >= threshold;

    lv_obj_set_style_bg_color(g_install_blocks[i], on ? lv_color_hex(0x4EA3FF) : lv_color_hex(0x0B1323), 0);
    lv_obj_set_style_border_color(g_install_blocks[i], on ? lv_color_hex(0x93C5FD) : lv_color_hex(0x1F2937), 0);
  }

  if (g_lbl_progress) {
    String text = String((int)pct) + "%";
    lv_label_set_text(g_lbl_progress, text.c_str());
  }
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
  m.short_status = "CZEKA";

  unsigned long now = millis();
  String st = s.status;
  st.toLowerCase();
  const String log = s.log;

  bool mode_has_ping = String(scan_mode_name()).indexOf("BCAST") >= 0;
  bool wifi_ok = s.wifi.startsWith("WiFi: ") && !s.wifi.startsWith("WiFi: OFF") && !s.wifi.startsWith("WiFi: laczenie");
  bool scan_started = s.running || (log.indexOf("SCAN: start (") >= 0);
  bool ping_wait_done = (log.indexOf("BCAST PING: czekano") >= 0);
  bool arp_started = (log.indexOf("DISCOVER: start (ARP") >= 0) || (st.indexOf("arp") >= 0);
  bool portscan_started = (log.indexOf("PORTSCAN: start") >= 0) || (st.indexOf("port scan") >= 0);
  bool portscan_skipped = (log.indexOf("PORTSCAN: pominieto") >= 0);
  bool portscan_done = (log.indexOf("PORTSCAN: koniec") >= 0) || portscan_skipped;
  bool finished = (st.indexOf("zakonczono") >= 0) || (st.indexOf("timeout") >= 0);
  bool error = (st.indexOf("brak wifi") >= 0) || (st.indexOf("blad") >= 0) ||
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

  if (error) m.short_status = "BLAD";
  else if (finished) m.short_status = "GOTOWE";
  else if (s.running) m.short_status = "TRWA";
  else m.short_status = "CZEKA";

  return m;
}

void create_gui() {
#if LVGL_VERSION_MAJOR >= 9
  lv_obj_t *scr = lv_screen_active();
#else
  lv_obj_t *scr = lv_scr_act();
#endif

  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  g_page_main = lv_obj_create(scr);
  lv_obj_set_size(g_page_main, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(g_page_main, 0, 0);
  lv_obj_set_style_bg_color(g_page_main, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_page_main, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_page_main, 0, 0);
  lv_obj_set_style_radius(g_page_main, 0, 0);
  lv_obj_set_style_pad_all(g_page_main, 0, 0);
  lv_obj_clear_flag(g_page_main, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_page_main, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_page_main, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  g_lbl_title = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_title, "LAN SCAN INSTALLER");
  lv_obj_set_style_text_color(g_lbl_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_title, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_title, LV_ALIGN_TOP_LEFT, 26, 16);

  g_lbl_hint_main = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_hint_main, "LEFT -> PORTS    RIGHT -> LOG");
  lv_obj_set_style_text_color(g_lbl_hint_main, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(g_lbl_hint_main, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_hint_main, LV_ALIGN_TOP_RIGHT, -24, 16);

  g_lbl_wifi = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_wifi, "WiFi: laczenie...");
  lv_obj_set_style_text_color(g_lbl_wifi, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_wifi, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_wifi, LV_ALIGN_TOP_LEFT, 26, 46);

  g_lbl_status = lv_label_create(g_page_main);
  lv_label_set_text(g_lbl_status, "STAN: CZEKA");
  lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_status, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_status, LV_ALIGN_TOP_RIGHT, -24, 46);

  g_lbl_mode = lv_label_create(g_page_main);
  String mode_line = String("MODE: ") + scan_mode_name() + "  PORTS: " + (is_port_profile_legacy() ? "0-4096" : "FULL");
  lv_label_set_text(g_lbl_mode, mode_line.c_str());
  lv_obj_set_style_text_color(g_lbl_mode, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(g_lbl_mode, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_mode, LV_ALIGN_TOP_MID, 0, 76);

  const int32_t card_y = 112;
  const int32_t gap = 18;
  const int32_t first_x = (LCD_WIDTH - (4 * 220 + 3 * gap)) / 2;
  g_card_start = create_step_card(g_page_main, first_x, card_y, "START", &g_lbl_start_state);
  g_card_discover = create_step_card(g_page_main, first_x + 220 + gap, card_y, "DISCOVER", &g_lbl_discover_state);
  g_card_ports = create_step_card(g_page_main, first_x + 2 * (220 + gap), card_y, "PORTS", &g_lbl_ports_state);
  g_card_finish = create_step_card(g_page_main, first_x + 3 * (220 + gap), card_y, "FINISH", &g_lbl_finish_state);

  g_panel_progress = lv_obj_create(g_page_main);
  lv_obj_set_size(g_panel_progress, LCD_WIDTH - 64, 140);
  lv_obj_align(g_panel_progress, LV_ALIGN_TOP_MID, 0, 306);
  lv_obj_set_style_bg_color(g_panel_progress, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_panel_progress, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_panel_progress, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(g_panel_progress, 2, 0);
  lv_obj_set_style_radius(g_panel_progress, 0, 0);
  lv_obj_set_style_pad_all(g_panel_progress, 12, 0);
  lv_obj_clear_flag(g_panel_progress, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_panel_progress, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_panel_progress, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  lv_obj_t *lbl_progress_title = lv_label_create(g_panel_progress);
  lv_label_set_text(lbl_progress_title, "INSTALL PROGRESS");
  lv_obj_set_style_text_color(lbl_progress_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_progress_title, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(lbl_progress_title, 8, 6);

  g_bar_strip = lv_obj_create(g_panel_progress);
  lv_obj_set_size(g_bar_strip, 790, 28);
  lv_obj_set_pos(g_bar_strip, 8, 50);
  lv_obj_set_style_bg_color(g_bar_strip, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_bar_strip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_bar_strip, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(g_bar_strip, 1, 0);
  lv_obj_set_style_radius(g_bar_strip, 0, 0);
  lv_obj_set_style_pad_all(g_bar_strip, 0, 0);
  lv_obj_clear_flag(g_bar_strip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_bar_strip, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_bar_strip, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

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
    lv_obj_set_style_bg_color(b, lv_color_hex(0x0B1323), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(b, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);
    g_install_blocks[i] = b;
    x += w + gap_px;
  }

  g_lbl_progress = lv_label_create(g_panel_progress);
  lv_label_set_text(g_lbl_progress, "0%");
  lv_obj_set_style_text_color(g_lbl_progress, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_progress, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_progress, LV_ALIGN_TOP_RIGHT, -10, 55);

  g_btn_ports_legacy = lv_btn_create(g_page_main);
  lv_obj_set_size(g_btn_ports_legacy, 170, 44);
  lv_obj_align(g_btn_ports_legacy, LV_ALIGN_BOTTOM_LEFT, 28, -84);
  lv_obj_set_style_bg_color(g_btn_ports_legacy, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(g_btn_ports_legacy, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_btn_ports_legacy, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(g_btn_ports_legacy, 2, 0);
  lv_obj_set_style_radius(g_btn_ports_legacy, 0, 0);
  lv_obj_add_event_cb(g_btn_ports_legacy, ev_set_ports_legacy, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(g_btn_ports_legacy, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_btn_ports_legacy, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  lv_obj_t *lbl_legacy = lv_label_create(g_btn_ports_legacy);
  lv_label_set_text(lbl_legacy, "PORTS 0-4096");
  lv_obj_set_style_text_color(lbl_legacy, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_legacy, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_legacy);

  g_btn_ports_full = lv_btn_create(g_page_main);
  lv_obj_set_size(g_btn_ports_full, 170, 44);
  lv_obj_align(g_btn_ports_full, LV_ALIGN_BOTTOM_RIGHT, -28, -84);
  lv_obj_set_style_bg_color(g_btn_ports_full, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(g_btn_ports_full, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_btn_ports_full, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(g_btn_ports_full, 2, 0);
  lv_obj_set_style_radius(g_btn_ports_full, 0, 0);
  lv_obj_add_event_cb(g_btn_ports_full, ev_set_ports_full, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(g_btn_ports_full, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_btn_ports_full, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  lv_obj_t *lbl_full = lv_label_create(g_btn_ports_full);
  lv_label_set_text(lbl_full, "PORTS FULL");
  lv_obj_set_style_text_color(lbl_full, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_full, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_full);

  g_btn_start = lv_btn_create(g_page_main);
  lv_obj_set_size(g_btn_start, 220, 52);
  lv_obj_align(g_btn_start, LV_ALIGN_BOTTOM_LEFT, 30, -18);
  lv_obj_set_style_bg_color(g_btn_start, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(g_btn_start, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_btn_start, lv_color_hex(0x93C5FD), 0);
  lv_obj_set_style_border_width(g_btn_start, 2, 0);
  lv_obj_set_style_radius(g_btn_start, 0, 0);
  lv_obj_add_event_cb(g_btn_start, ev_start_scan, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(g_btn_start, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_btn_start, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  lv_obj_t *lbl_local = lv_label_create(g_btn_start);
  lv_label_set_text(lbl_local, "LOCAL");
  lv_obj_set_style_text_color(lbl_local, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_local, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_local);

  lv_obj_align(g_btn_start, LV_ALIGN_BOTTOM_MID, 0, -18);

  g_page_log = lv_obj_create(scr);
  lv_obj_set_size(g_page_log, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(g_page_log, 0, 0);
  lv_obj_set_style_bg_color(g_page_log, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_page_log, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_page_log, 0, 0);
  lv_obj_set_style_radius(g_page_log, 0, 0);
  lv_obj_set_style_pad_all(g_page_log, 0, 0);
  lv_obj_clear_flag(g_page_log, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_page_log, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_page_log, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  g_lbl_log_title = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_log_title, "LIVE LOG");
  lv_obj_set_style_text_color(g_lbl_log_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_log_title, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_log_title, LV_ALIGN_TOP_LEFT, 24, 16);

  g_lbl_hint_log = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_hint_log, "SWIPE LEFT <- MAIN");
  lv_obj_set_style_text_color(g_lbl_hint_log, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(g_lbl_hint_log, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_hint_log, LV_ALIGN_TOP_RIGHT, -24, 16);

  g_lbl_log_wifi = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_log_wifi, "WiFi: laczenie...");
  lv_obj_set_style_text_color(g_lbl_log_wifi, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_log_wifi, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_log_wifi, LV_ALIGN_TOP_LEFT, 24, 48);

  g_lbl_log_status = lv_label_create(g_page_log);
  lv_label_set_text(g_lbl_log_status, "STAN: CZEKA  0/0");
  lv_obj_set_style_text_color(g_lbl_log_status, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_log_status, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_log_status, LV_ALIGN_TOP_RIGHT, -24, 48);

  g_panel_log = lv_obj_create(g_page_log);
  lv_obj_set_size(g_panel_log, 692, 500);
  lv_obj_align(g_panel_log, LV_ALIGN_TOP_LEFT, 24, 84);
  lv_obj_set_style_bg_color(g_panel_log, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_panel_log, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_panel_log, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(g_panel_log, 2, 0);
  lv_obj_set_style_radius(g_panel_log, 0, 0);
  lv_obj_set_style_pad_all(g_panel_log, 10, 0);
  lv_obj_set_scrollbar_mode(g_panel_log, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(g_panel_log, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_panel_log, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  lv_obj_t *lbl_log_caption = lv_label_create(g_panel_log);
  lv_label_set_text(lbl_log_caption, "EVENTS");
  lv_obj_set_style_text_color(lbl_log_caption, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_log_caption, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_log_caption, LV_ALIGN_TOP_LEFT, 0, 0);

  g_lbl_log = lv_label_create(g_panel_log);
  lv_obj_set_width(g_lbl_log, 664);
  lv_label_set_long_mode(g_lbl_log, LV_LABEL_LONG_WRAP);
  lv_label_set_text(g_lbl_log, "Czekam na logi...\n");
  lv_obj_set_style_text_color(g_lbl_log, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_log, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_log, LV_ALIGN_TOP_LEFT, 0, 28);

  g_panel_hosts = lv_obj_create(g_page_log);
  lv_obj_set_size(g_panel_hosts, 284, 500);
  lv_obj_align(g_panel_hosts, LV_ALIGN_TOP_RIGHT, -24, 84);
  lv_obj_set_style_bg_color(g_panel_hosts, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_panel_hosts, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_panel_hosts, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(g_panel_hosts, 2, 0);
  lv_obj_set_style_radius(g_panel_hosts, 0, 0);
  lv_obj_set_style_pad_all(g_panel_hosts, 10, 0);
  lv_obj_set_scrollbar_mode(g_panel_hosts, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(g_panel_hosts, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_panel_hosts, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  lv_obj_t *lbl_hosts_caption = lv_label_create(g_panel_hosts);
  lv_label_set_text(lbl_hosts_caption, "HOSTY");
  lv_obj_set_style_text_color(lbl_hosts_caption, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_hosts_caption, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_hosts_caption, LV_ALIGN_TOP_LEFT, 0, 0);

  g_lbl_ips = lv_label_create(g_panel_hosts);
  lv_obj_set_width(g_lbl_ips, 254);
  lv_label_set_long_mode(g_lbl_ips, LV_LABEL_LONG_WRAP);
  lv_label_set_text(g_lbl_ips, "Brak wykrytych IP.");
  lv_obj_set_style_text_color(g_lbl_ips, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_ips, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_ips, LV_ALIGN_TOP_LEFT, 0, 28);

  g_page_ports = lv_obj_create(scr);
  lv_obj_set_size(g_page_ports, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(g_page_ports, 0, 0);
  lv_obj_set_style_bg_color(g_page_ports, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_page_ports, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_page_ports, 0, 0);
  lv_obj_set_style_radius(g_page_ports, 0, 0);
  lv_obj_set_style_pad_all(g_page_ports, 0, 0);
  lv_obj_clear_flag(g_page_ports, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_page_ports, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_page_ports, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  g_lbl_ports_title = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_ports_title, "HOSTY I OTWARTE PORTY");
  lv_obj_set_style_text_color(g_lbl_ports_title, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(g_lbl_ports_title, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_ports_title, LV_ALIGN_TOP_LEFT, 24, 16);

  g_lbl_hint_ports = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_hint_ports, "SWIPE RIGHT -> MAIN");
  lv_obj_set_style_text_color(g_lbl_hint_ports, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_text_font(g_lbl_hint_ports, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_hint_ports, LV_ALIGN_TOP_RIGHT, -24, 16);

  g_lbl_ports_wifi = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_ports_wifi, "WiFi: laczenie...");
  lv_obj_set_style_text_color(g_lbl_ports_wifi, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_ports_wifi, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_ports_wifi, LV_ALIGN_TOP_LEFT, 24, 48);

  g_lbl_ports_status = lv_label_create(g_page_ports);
  lv_label_set_text(g_lbl_ports_status, "STAN: CZEKA  0/0");
  lv_obj_set_style_text_color(g_lbl_ports_status, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_ports_status, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_ports_status, LV_ALIGN_TOP_RIGHT, -24, 48);

  g_panel_ports = lv_obj_create(g_page_ports);
  lv_obj_set_size(g_panel_ports, LCD_WIDTH - 48, 500);
  lv_obj_align(g_panel_ports, LV_ALIGN_TOP_MID, 0, 84);
  lv_obj_set_style_bg_color(g_panel_ports, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_panel_ports, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_panel_ports, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(g_panel_ports, 2, 0);
  lv_obj_set_style_radius(g_panel_ports, 0, 0);
  lv_obj_set_style_pad_all(g_panel_ports, 10, 0);
  lv_obj_set_scrollbar_mode(g_panel_ports, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(g_panel_ports, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_panel_ports, ev_horizontal_nav, LV_EVENT_GESTURE, nullptr);

  lv_obj_t *lbl_ports_caption = lv_label_create(g_panel_ports);
  lv_label_set_text(lbl_ports_caption, "IP -> OPEN PORTS");
  lv_obj_set_style_text_color(lbl_ports_caption, lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_text_font(lbl_ports_caption, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_ports_caption, LV_ALIGN_TOP_LEFT, 0, 0);

  g_lbl_open_ports = lv_label_create(g_panel_ports);
  lv_obj_set_width(g_lbl_open_ports, LCD_WIDTH - 80);
  lv_label_set_long_mode(g_lbl_open_ports, LV_LABEL_LONG_WRAP);
  lv_label_set_text(g_lbl_open_ports, "Brak hostow z otwartymi portami.");
  lv_obj_set_style_text_color(g_lbl_open_ports, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(g_lbl_open_ports, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_open_ports, LV_ALIGN_TOP_LEFT, 0, 28);

  set_page(PAGE_MAIN);
  set_install_bar_percent(0);

  AppSnapshot init_snapshot;
  snapshot_state(init_snapshot);
  ScanUiModel init_model = compute_ui_model(init_snapshot);
  set_step_card_state(g_card_start, g_lbl_start_state, init_model.start_state, false);
  set_step_card_state(g_card_discover, g_lbl_discover_state, init_model.discover_state, false);
  set_step_card_state(g_card_ports, g_lbl_ports_state, init_model.ports_state, false);
  set_step_card_state(g_card_finish, g_lbl_finish_state, init_model.finish_state, init_model.error);
  set_install_bar_percent(init_model.progress_percent);
  update_port_profile_buttons();
}

void refresh_ui() {
  AppSnapshot s;
  snapshot_state(s);
  ScanUiModel m = compute_ui_model(s);

  String wifi_line = s.wifi;
  if (wifi_line.startsWith("WiFi: ") && !wifi_line.startsWith("WiFi: OFF") && !wifi_line.startsWith("WiFi: laczenie")) {
    wifi_line = "WiFi OK  " + wifi_line.substring(6);
  }

  if (wifi_line != g_ui_last_wifi) {
    g_ui_last_wifi = wifi_line;
    if (g_lbl_wifi) lv_label_set_text(g_lbl_wifi, wifi_line.c_str());
    if (g_lbl_log_wifi) lv_label_set_text(g_lbl_log_wifi, wifi_line.c_str());
    if (g_lbl_ports_wifi) lv_label_set_text(g_lbl_ports_wifi, wifi_line.c_str());
  }

  if (s.status != g_ui_last_status || (int)m.progress_percent != g_ui_last_percent ||
      s.done != g_ui_last_done || s.total != g_ui_last_total) {
    g_ui_last_status = s.status;
    g_ui_last_percent = (int)m.progress_percent;
    g_ui_last_done = s.done;
    g_ui_last_total = s.total;

    String short_state = String("STAN: ") + m.short_status;
    if (g_lbl_status) lv_label_set_text(g_lbl_status, short_state.c_str());

    String log_state = short_state + "  " + String(s.done) + "/" + String(s.total);
    if (g_lbl_log_status) lv_label_set_text(g_lbl_log_status, log_state.c_str());
    if (g_lbl_ports_status) lv_label_set_text(g_lbl_ports_status, log_state.c_str());
  }

  if (g_lbl_mode) {
    String mode_line = String("MODE: ") + scan_mode_name() + "  PORTS: " + (is_port_profile_legacy() ? "0-4096" : "FULL");
    lv_label_set_text(g_lbl_mode, mode_line.c_str());
  }
  update_port_profile_buttons();

  set_step_card_state(g_card_start, g_lbl_start_state, m.start_state, false);
  set_step_card_state(g_card_discover, g_lbl_discover_state, m.discover_state, false);
  set_step_card_state(g_card_ports, g_lbl_ports_state, m.ports_state, false);
  set_step_card_state(g_card_finish, g_lbl_finish_state, m.finish_state, m.error);
  set_install_bar_percent(m.progress_percent);

  static unsigned long last_log_refresh = 0;
  unsigned long now = millis();
  if (s.log != g_ui_last_log && (now - last_log_refresh) >= 120) {
    last_log_refresh = now;
    g_ui_last_log = s.log;

    if (g_lbl_log) {
      String recent = extract_recent_lines(s.log, 20);
      if (!recent.length()) recent = "Czekam na logi...\n";
      lv_label_set_text(g_lbl_log, recent.c_str());
    }
    if (g_panel_log) {
      lv_obj_scroll_to_y(g_panel_log, LV_COORD_MAX, LV_ANIM_OFF);
    }

    String ips = collect_discovered_ips(s.log);
    if (ips != g_ui_last_ips && g_lbl_ips) {
      g_ui_last_ips = ips;
      if (!ips.length()) {
        lv_label_set_text(g_lbl_ips, "Brak wykrytych IP.");
      } else {
        int cnt = count_lines(ips);
        String txt = String("Hosty: ") + cnt + "\n\n" + ips;
        lv_label_set_text(g_lbl_ips, txt.c_str());
      }
    }

    String open_ports = collect_open_ports_by_ip(s.log);
    if (open_ports != g_ui_last_open_ports && g_lbl_open_ports) {
      g_ui_last_open_ports = open_ports;
      if (!open_ports.length()) {
        lv_label_set_text(g_lbl_open_ports, "Brak hostow z otwartymi portami.");
      } else {
        int cnt = count_lines(open_ports);
        String txt = String("Hosty z open portami: ") + cnt + "\n\n" + open_ports;
        lv_label_set_text(g_lbl_open_ports, txt.c_str());
      }
    }
  }

  if (g_btn_start) {
    if (s.running) lv_obj_add_state(g_btn_start, LV_STATE_DISABLED);
    else lv_obj_clear_state(g_btn_start, LV_STATE_DISABLED);
  }

  static unsigned long last_wifi_refresh = 0;
  if ((now - last_wifi_refresh) > 1200) {
    last_wifi_refresh = now;
    if (WiFi.status() == WL_CONNECTED) set_wifi_text(String("WiFi: ") + WiFi.localIP().toString());
    else if (s.running) set_wifi_text("WiFi: laczenie...");
  }
}
