#include "gui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

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
bool start_bridge_scan_request(const char *source);
void snapshot_state(AppSnapshot &out);
void set_wifi_text(const String &s);
const char *scan_mode_name();

enum StepState {
  STEP_PENDING,
  STEP_ACTIVE,
  STEP_DONE,
  STEP_SKIPPED,
};

enum StageId {
  STAGE_NONE,
  STAGE_WIFI,
  STAGE_PING,
  STAGE_WAIT,
  STAGE_ARP,
  STAGE_PORTSCAN,
  STAGE_DONE,
};

static String g_ui_last_wifi = "";
static String g_ui_last_status = "";
static String g_ui_last_log = "";
static String g_ui_last_ips = "";
static uint32_t g_ui_last_done = UINT32_MAX;
static uint32_t g_ui_last_total = UINT32_MAX;

static lv_obj_t *g_lbl_title = nullptr;
static lv_obj_t *g_lbl_wifi = nullptr;
static lv_obj_t *g_lbl_status = nullptr;
static lv_obj_t *g_lbl_progress = nullptr;

static lv_obj_t *g_lbl_mode = nullptr;
static lv_obj_t *g_lbl_step_wifi = nullptr;
static lv_obj_t *g_lbl_step_ping = nullptr;
static lv_obj_t *g_lbl_step_wait = nullptr;
static lv_obj_t *g_lbl_step_arp = nullptr;
static lv_obj_t *g_lbl_step_ports = nullptr;
static lv_obj_t *g_lbl_step_done = nullptr;
static lv_obj_t *g_lbl_algo_note = nullptr;

static lv_obj_t *g_panel_log = nullptr;
static lv_obj_t *g_lbl_log = nullptr;

static lv_obj_t *g_btn_start = nullptr;
static lv_obj_t *g_btn_bridge = nullptr;
static lv_obj_t *g_sheet = nullptr;
static lv_obj_t *g_lbl_ips = nullptr;
static lv_obj_t *g_lbl_sheet_hint = nullptr;

static bool g_sheet_open = false;
static int32_t g_sheet_y_closed = 0;
static int32_t g_sheet_y_open = 0;

static StageId g_stage_active = STAGE_NONE;
static unsigned long g_stage_start_ms = 0;

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

static int count_lines(const String &text) {
  if (!text.length()) return 0;
  int n = 1;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (text[i] == '\n') n++;
  }
  return n;
}

static void anim_set_translate_y(void *obj, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}

static void anim_set_opa(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void animate_widget(lv_obj_t *obj, uint32_t delay_ms, int32_t from_y) {
  lv_obj_set_style_translate_y(obj, from_y, 0);
  lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);

  lv_anim_t a1;
  lv_anim_init(&a1);
  lv_anim_set_var(&a1, obj);
  lv_anim_set_exec_cb(&a1, anim_set_translate_y);
  lv_anim_set_values(&a1, from_y, 0);
  lv_anim_set_time(&a1, 420);
  lv_anim_set_delay(&a1, delay_ms);
  lv_anim_set_path_cb(&a1, lv_anim_path_ease_out);
  lv_anim_start(&a1);

  lv_anim_t a2;
  lv_anim_init(&a2);
  lv_anim_set_var(&a2, obj);
  lv_anim_set_exec_cb(&a2, anim_set_opa);
  lv_anim_set_values(&a2, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_time(&a2, 520);
  lv_anim_set_delay(&a2, delay_ms);
  lv_anim_set_path_cb(&a2, lv_anim_path_linear);
  lv_anim_start(&a2);
}

static void sheet_set_y(void *obj, int32_t y) {
  lv_obj_set_y((lv_obj_t *)obj, y);
}

static void toggle_sheet(bool open) {
  if (!g_sheet) return;
  g_sheet_open = open;

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g_sheet);
  lv_anim_set_exec_cb(&a, sheet_set_y);
  lv_anim_set_time(&a, 260);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_values(&a, lv_obj_get_y(g_sheet), open ? g_sheet_y_open : g_sheet_y_closed);
  lv_anim_start(&a);

  if (g_lbl_sheet_hint) {
    lv_label_set_text(g_lbl_sheet_hint, open ? "Przesun w dol aby zamknac liste IP" : "Przesun w gore aby otworzyc liste IP");
  }
}

static void ev_sheet_toggle(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  toggle_sheet(!g_sheet_open);
}

static void ev_screen_gesture(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;

  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_TOP) toggle_sheet(true);
  if (dir == LV_DIR_BOTTOM) toggle_sheet(false);
}

static void ev_start_scan(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  (void)start_scan_request("UI: start local scan");
}

static void ev_start_bridge_scan(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  (void)start_bridge_scan_request("UI: start bridge scan");
}

static void set_step_label(lv_obj_t *lbl, const char *name, StepState state,
                           int percent, int eta_s, uint32_t done, uint32_t total) {
  if (!lbl) return;

  const char *prefix = "[ ]";
  lv_color_t color = lv_color_hex(0x9CA3AF);

  if (state == STEP_ACTIVE) {
    prefix = "[>]";
    color = lv_color_hex(0xFFFFFF);
  } else if (state == STEP_DONE) {
    prefix = "[x]";
    color = lv_color_hex(0x22C55E);
  } else if (state == STEP_SKIPPED) {
    prefix = "[-]";
    color = lv_color_hex(0x6B7280);
  }

  String text = String(prefix) + " " + name;
  if (percent >= 0) {
    text += "  ";
    text += percent;
    text += "%";
  }
  if (total > 0) {
    text += " (";
    text += done;
    text += "/";
    text += total;
    text += ")";
  }
  if (eta_s >= 0 && state == STEP_ACTIVE) {
    text += "  ETA ";
    text += eta_s;
    text += "s";
  }

  lv_label_set_text(lbl, text.c_str());
  lv_obj_set_style_text_color(lbl, color, 0);
}

static void update_algorithm_view(const AppSnapshot &s) {
  unsigned long now = millis();

  String st = s.status;
  st.toLowerCase();

  const String log = s.log;
  bool mode_has_ping = String(scan_mode_name()).indexOf("BCAST") >= 0;

  bool wifi_ok = s.wifi.startsWith("WiFi: ") && !s.wifi.startsWith("WiFi: OFF") && !s.wifi.startsWith("WiFi: laczenie");
  bool scan_started = s.running || (log.indexOf("SCAN: start (") >= 0);
  bool ping_sent = (log.indexOf("BCAST PING: wyslano") >= 0) || (log.indexOf("BCAST PING: blad wysylki") >= 0);
  bool ping_wait_done = (log.indexOf("BCAST PING: czekano") >= 0);
  bool arp_started = (log.indexOf("DISCOVER: start (ARP") >= 0) || (st.indexOf("arp") >= 0);
  bool portscan_started = (log.indexOf("PORTSCAN: start") >= 0) || (st.indexOf("port scan") >= 0);
  bool portscan_skipped = (log.indexOf("PORTSCAN: pominieto") >= 0);
  bool portscan_done = (log.indexOf("PORTSCAN: koniec") >= 0) || portscan_skipped;
  bool finished = (st.indexOf("zakonczono") >= 0) || (st.indexOf("timeout") >= 0);
  bool error = (st.indexOf("brak wifi") >= 0) || (st.indexOf("blad") >= 0);

  StepState wifi_state = STEP_PENDING;
  if (wifi_ok) wifi_state = STEP_DONE;
  else if (scan_started || st.indexOf("laczenie wifi") >= 0 || s.wifi.startsWith("WiFi: laczenie")) wifi_state = STEP_ACTIVE;

  StepState ping_state = STEP_PENDING;
  StepState wait_state = STEP_PENDING;
  if (!mode_has_ping) {
    ping_state = STEP_SKIPPED;
    wait_state = STEP_SKIPPED;
  } else {
    if (ping_wait_done || arp_started || finished) ping_state = STEP_DONE;
    else if (ping_sent || st.indexOf("ping broadcast") >= 0) ping_state = STEP_ACTIVE;

    if (ping_wait_done || arp_started || finished) wait_state = STEP_DONE;
    else if (ping_sent) wait_state = STEP_ACTIVE;
  }

  StepState arp_state = STEP_PENDING;
  if (arp_started && !portscan_started && !finished) arp_state = STEP_ACTIVE;
  if (arp_started && (portscan_started || finished || !s.running)) arp_state = STEP_DONE;

  StepState port_state = STEP_PENDING;
  if (portscan_skipped) port_state = STEP_SKIPPED;
  else if (portscan_started && !portscan_done && !finished) port_state = STEP_ACTIVE;
  else if (portscan_done || (portscan_started && !s.running)) port_state = STEP_DONE;

  StepState done_state = STEP_PENDING;
  if (finished || error) done_state = STEP_DONE;
  else if (s.running && (port_state == STEP_ACTIVE)) done_state = STEP_ACTIVE;

  StageId current_stage = STAGE_NONE;
  if (done_state == STEP_DONE) current_stage = STAGE_DONE;
  else if (port_state == STEP_ACTIVE) current_stage = STAGE_PORTSCAN;
  else if (arp_state == STEP_ACTIVE) current_stage = STAGE_ARP;
  else if (wait_state == STEP_ACTIVE) current_stage = STAGE_WAIT;
  else if (ping_state == STEP_ACTIVE) current_stage = STAGE_PING;
  else if (wifi_state == STEP_ACTIVE) current_stage = STAGE_WIFI;

  if (current_stage != g_stage_active) {
    g_stage_active = current_stage;
    g_stage_start_ms = now;
  }

  int wifi_pct = -1;
  int wifi_eta = -1;
  if (wifi_state == STEP_ACTIVE && SCAN_WIFI_CONNECT_TIMEOUT_MS > 0) {
    unsigned long elapsed = now - g_stage_start_ms;
    wifi_pct = (int)((elapsed * 100UL) / SCAN_WIFI_CONNECT_TIMEOUT_MS);
    if (wifi_pct > 99) wifi_pct = 99;
    unsigned long rem = (elapsed < SCAN_WIFI_CONNECT_TIMEOUT_MS) ? (SCAN_WIFI_CONNECT_TIMEOUT_MS - elapsed) : 0;
    wifi_eta = (int)(rem / 1000UL);
  } else if (wifi_state == STEP_DONE) {
    wifi_pct = 100;
  }

  int ping_pct = -1;
  int ping_eta = -1;
  if (ping_state == STEP_ACTIVE) {
    unsigned long elapsed = now - g_stage_start_ms;
    ping_pct = (int)((elapsed * 100UL) / 1200UL);
    if (ping_pct > 95) ping_pct = 95;
    ping_eta = (int)((1200UL > elapsed ? (1200UL - elapsed) : 0) / 1000UL);
  } else if (ping_state == STEP_DONE || ping_state == STEP_SKIPPED) {
    ping_pct = 100;
  }

  int wait_pct = -1;
  int wait_eta = -1;
  if (wait_state == STEP_ACTIVE && SCAN_BROADCAST_WAIT_MS > 0) {
    unsigned long elapsed = now - g_stage_start_ms;
    wait_pct = (int)((elapsed * 100UL) / SCAN_BROADCAST_WAIT_MS);
    if (wait_pct > 99) wait_pct = 99;
    unsigned long rem = (elapsed < SCAN_BROADCAST_WAIT_MS) ? (SCAN_BROADCAST_WAIT_MS - elapsed) : 0;
    wait_eta = (int)(rem / 1000UL);
  } else if (wait_state == STEP_DONE || wait_state == STEP_SKIPPED) {
    wait_pct = 100;
  }

  int arp_pct = -1;
  int arp_eta = -1;
  if (arp_state == STEP_ACTIVE) {
    if (s.total > 0) {
      arp_pct = (int)((s.done * 100UL) / s.total);
      if (arp_pct > 100) arp_pct = 100;
    }
    if (s.done > 0 && s.total > 0) {
      unsigned long elapsed = now - g_stage_start_ms;
      unsigned long rem_items = s.total - s.done;
      unsigned long eta_ms = (unsigned long)((double)elapsed * (double)rem_items / (double)s.done);
      arp_eta = (int)(eta_ms / 1000UL);
    } else {
      arp_eta = (int)(SCAN_ARP_TOTAL_WAIT_MS / 1000UL);
    }
  } else if (arp_state == STEP_DONE) {
    arp_pct = 100;
  }

  int port_pct = -1;
  int port_eta = -1;
  if (port_state == STEP_ACTIVE) {
    if (s.total > 0) {
      port_pct = (int)((s.done * 100UL) / s.total);
      if (port_pct > 100) port_pct = 100;
    }
    if (s.done > 0 && s.total > 0) {
      unsigned long elapsed = now - g_stage_start_ms;
      unsigned long rem_items = s.total - s.done;
      unsigned long eta_ms = (unsigned long)((double)elapsed * (double)rem_items / (double)s.done);
      port_eta = (int)(eta_ms / 1000UL);
    }
  } else if (port_state == STEP_DONE) {
    port_pct = 100;
  }

  int done_pct = (done_state == STEP_DONE) ? 100 : -1;

  set_step_label(g_lbl_step_wifi, "Polacz WiFi", wifi_state, wifi_pct, wifi_eta, 0, 0);
  set_step_label(g_lbl_step_ping, "Broadcast PING", ping_state, ping_pct, ping_eta, 0, 0);
  set_step_label(g_lbl_step_wait, "Czekaj 10 s", wait_state, wait_pct, wait_eta, 0, 0);
  set_step_label(g_lbl_step_arp, "ARP scan hostow", arp_state, arp_pct, arp_eta,
                 (arp_state == STEP_ACTIVE) ? s.done : 0, (arp_state == STEP_ACTIVE) ? s.total : 0);
  set_step_label(g_lbl_step_ports, "PORT scan 1..65535", port_state, port_pct, port_eta,
                 (port_state == STEP_ACTIVE) ? s.done : 0, (port_state == STEP_ACTIVE) ? s.total : 0);
  set_step_label(g_lbl_step_done, "Koniec / timeout", done_state, done_pct, -1, 0, 0);

  if (g_lbl_algo_note) {
    String note;
    if (error) note = "Stan: blad. Sprawdz WiFi i siec.";
    else if (finished) note = "Stan: skan zakonczony.";
    else if (port_state == STEP_ACTIVE) note = "Stan: skan wszystkich portow 1..65535.";
    else if (arp_state == STEP_ACTIVE) note = "Stan: ARP skanuje hosty po kolei.";
    else if (wait_state == STEP_ACTIVE) note = "Stan: po PING trwa okno oczekiwania.";
    else if (ping_state == STEP_ACTIVE) note = "Stan: wysylanie PING broadcast.";
    else if (wifi_state == STEP_ACTIVE) note = "Stan: nawiazywanie polaczenia WiFi.";
    else note = "Stan: gotowy. Kliknij LOCAL lub BRIDGE.";
    lv_label_set_text(g_lbl_algo_note, note.c_str());
  }
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
  lv_obj_add_event_cb(scr, ev_screen_gesture, LV_EVENT_GESTURE, nullptr);

  g_lbl_title = lv_label_create(scr);
  lv_label_set_text(g_lbl_title, "ESP32 LAN SCANNER");
  lv_obj_set_style_text_color(g_lbl_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_lbl_title, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_title, LV_ALIGN_TOP_MID, 0, 14);

  g_lbl_wifi = lv_label_create(scr);
  lv_label_set_text(g_lbl_wifi, "WiFi: laczenie...");
  lv_obj_set_style_text_color(g_lbl_wifi, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_lbl_wifi, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_wifi, LV_ALIGN_TOP_LEFT, 24, 44);

  g_lbl_status = lv_label_create(scr);
  lv_label_set_text(g_lbl_status, "Status: gotowy");
  lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_lbl_status, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_status, LV_ALIGN_TOP_LEFT, 24, 68);

  g_lbl_progress = lv_label_create(scr);
  lv_label_set_text(g_lbl_progress, "Postep: 0/0");
  lv_obj_set_style_text_color(g_lbl_progress, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_lbl_progress, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_progress, LV_ALIGN_TOP_LEFT, 24, 92);

  int32_t panel_y = 122;
  int32_t panel_h = LCD_HEIGHT - 206;
  int32_t panel_w = (LCD_WIDTH - 72) / 2;

  lv_obj_t *panel_algo = lv_obj_create(scr);
  lv_obj_set_size(panel_algo, panel_w, panel_h);
  lv_obj_align(panel_algo, LV_ALIGN_TOP_LEFT, 24, panel_y);
  lv_obj_set_style_bg_color(panel_algo, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(panel_algo, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(panel_algo, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(panel_algo, 1, 0);
  lv_obj_set_style_radius(panel_algo, 0, 0);
  lv_obj_set_style_pad_all(panel_algo, 12, 0);
  lv_obj_clear_flag(panel_algo, LV_OBJ_FLAG_SCROLLABLE);

  g_lbl_mode = lv_label_create(panel_algo);
  lv_label_set_text(g_lbl_mode, "ALGORYTM");
  lv_obj_set_style_text_color(g_lbl_mode, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_lbl_mode, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_mode, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *lbl_mode_val = lv_label_create(panel_algo);
  String mode_line = String("Local: ") + scan_mode_name() + " | Bridge: " + BRIDGE_SERVER_IP;
  lv_label_set_text(lbl_mode_val, mode_line.c_str());
  lv_obj_set_style_text_color(lbl_mode_val, lv_color_hex(0xD1D5DB), 0);
  lv_obj_set_style_text_font(lbl_mode_val, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_mode_val, LV_ALIGN_TOP_LEFT, 0, 26);

  g_lbl_step_wifi = lv_label_create(panel_algo);
  lv_obj_set_style_text_font(g_lbl_step_wifi, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_step_wifi, LV_ALIGN_TOP_LEFT, 0, 66);

  g_lbl_step_ping = lv_label_create(panel_algo);
  lv_obj_set_style_text_font(g_lbl_step_ping, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_step_ping, LV_ALIGN_TOP_LEFT, 0, 94);

  g_lbl_step_wait = lv_label_create(panel_algo);
  lv_obj_set_style_text_font(g_lbl_step_wait, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_step_wait, LV_ALIGN_TOP_LEFT, 0, 122);

  g_lbl_step_arp = lv_label_create(panel_algo);
  lv_obj_set_style_text_font(g_lbl_step_arp, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_step_arp, LV_ALIGN_TOP_LEFT, 0, 150);

  g_lbl_step_ports = lv_label_create(panel_algo);
  lv_obj_set_style_text_font(g_lbl_step_ports, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_step_ports, LV_ALIGN_TOP_LEFT, 0, 178);

  g_lbl_step_done = lv_label_create(panel_algo);
  lv_obj_set_style_text_font(g_lbl_step_done, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_step_done, LV_ALIGN_TOP_LEFT, 0, 206);

  g_lbl_algo_note = lv_label_create(panel_algo);
  lv_obj_set_width(g_lbl_algo_note, panel_w - 24);
  lv_label_set_long_mode(g_lbl_algo_note, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(g_lbl_algo_note, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_lbl_algo_note, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_algo_note, LV_ALIGN_BOTTOM_LEFT, 0, -8);

  g_panel_log = lv_obj_create(scr);
  lv_obj_set_size(g_panel_log, panel_w, panel_h);
  lv_obj_align(g_panel_log, LV_ALIGN_TOP_RIGHT, -24, panel_y);
  lv_obj_set_style_bg_color(g_panel_log, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(g_panel_log, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_panel_log, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(g_panel_log, 1, 0);
  lv_obj_set_style_radius(g_panel_log, 0, 0);
  lv_obj_set_style_pad_all(g_panel_log, 12, 0);
  lv_obj_set_scrollbar_mode(g_panel_log, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t *lbl_log_title = lv_label_create(g_panel_log);
  lv_label_set_text(lbl_log_title, "LIVE LOG");
  lv_obj_set_style_text_color(lbl_log_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_log_title, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_log_title, LV_ALIGN_TOP_LEFT, 0, 0);

  g_lbl_log = lv_label_create(g_panel_log);
  lv_obj_set_width(g_lbl_log, panel_w - 24);
  lv_label_set_long_mode(g_lbl_log, LV_LABEL_LONG_WRAP);
  lv_label_set_text(g_lbl_log, "Czekam na logi...\n");
  lv_obj_set_style_text_color(g_lbl_log, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_lbl_log, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_log, LV_ALIGN_TOP_LEFT, 0, 26);

  g_btn_start = lv_btn_create(scr);
  lv_obj_set_size(g_btn_start, 240, 54);
  lv_obj_align(g_btn_start, LV_ALIGN_BOTTOM_LEFT, 20, -16);
  lv_obj_set_style_bg_color(g_btn_start, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(g_btn_start, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_btn_start, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(g_btn_start, 1, 0);
  lv_obj_set_style_radius(g_btn_start, 4, 0);
  lv_obj_add_event_cb(g_btn_start, ev_start_scan, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_start = lv_label_create(g_btn_start);
  lv_label_set_text(lbl_start, "LOCAL SCAN");
  lv_obj_set_style_text_color(lbl_start, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_font(lbl_start, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_start);

  g_btn_bridge = lv_btn_create(scr);
  lv_obj_set_size(g_btn_bridge, 240, 54);
  lv_obj_align(g_btn_bridge, LV_ALIGN_BOTTOM_RIGHT, -20, -16);
  lv_obj_set_style_bg_color(g_btn_bridge, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(g_btn_bridge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_btn_bridge, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(g_btn_bridge, 1, 0);
  lv_obj_set_style_radius(g_btn_bridge, 4, 0);
  lv_obj_add_event_cb(g_btn_bridge, ev_start_bridge_scan, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_bridge = lv_label_create(g_btn_bridge);
  lv_label_set_text(lbl_bridge, "BRIDGE SCAN");
  lv_obj_set_style_text_color(lbl_bridge, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_font(lbl_bridge, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_bridge);

  g_sheet = lv_obj_create(scr);
  int32_t sheet_h = LCD_HEIGHT - 120;
  lv_obj_set_size(g_sheet, LCD_WIDTH, sheet_h);
  g_sheet_y_open = LCD_HEIGHT - sheet_h;
  g_sheet_y_closed = LCD_HEIGHT - 10;
  lv_obj_set_pos(g_sheet, 0, g_sheet_y_closed);
  lv_obj_set_style_bg_color(g_sheet, lv_color_hex(0x0A0A0A), 0);
  lv_obj_set_style_bg_opa(g_sheet, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g_sheet, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(g_sheet, 1, 0);
  lv_obj_set_style_radius(g_sheet, 10, 0);
  lv_obj_set_style_pad_all(g_sheet, 12, 0);
  lv_obj_set_scrollbar_mode(g_sheet, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t *sheet_handle = lv_obj_create(g_sheet);
  lv_obj_set_size(sheet_handle, 130, 12);
  lv_obj_align(sheet_handle, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(sheet_handle, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(sheet_handle, LV_OPA_60, 0);
  lv_obj_set_style_border_width(sheet_handle, 0, 0);
  lv_obj_set_style_radius(sheet_handle, 6, 0);
  lv_obj_add_event_cb(sheet_handle, ev_sheet_toggle, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *sheet_title = lv_label_create(g_sheet);
  lv_label_set_text(sheet_title, "WYKRYTE HOSTY (IP)");
  lv_obj_set_style_text_color(sheet_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(sheet_title, &lv_font_montserrat_14, 0);
  lv_obj_align(sheet_title, LV_ALIGN_TOP_LEFT, 0, 20);

  g_lbl_ips = lv_label_create(g_sheet);
  lv_obj_set_width(g_lbl_ips, LCD_WIDTH - 30);
  lv_label_set_long_mode(g_lbl_ips, LV_LABEL_LONG_WRAP);
  lv_label_set_text(g_lbl_ips, "Brak wykrytych IP.");
  lv_obj_set_style_text_color(g_lbl_ips, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_lbl_ips, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_ips, LV_ALIGN_TOP_LEFT, 0, 48);

  g_lbl_sheet_hint = lv_label_create(scr);
  lv_label_set_text(g_lbl_sheet_hint, "Przesun w gore aby otworzyc liste IP");
  lv_obj_set_style_text_color(g_lbl_sheet_hint, lv_color_hex(0xD1D5DB), 0);
  lv_obj_set_style_text_font(g_lbl_sheet_hint, &lv_font_montserrat_14, 0);
  lv_obj_align(g_lbl_sheet_hint, LV_ALIGN_BOTTOM_RIGHT, -18, -4);

  AppSnapshot init_snapshot;
  snapshot_state(init_snapshot);
  update_algorithm_view(init_snapshot);

  animate_widget(g_lbl_title, 0, 14);
  animate_widget(g_lbl_wifi, 80, 14);
  animate_widget(g_lbl_status, 120, 14);
  animate_widget(g_lbl_progress, 160, 14);
  animate_widget(panel_algo, 220, 22);
  animate_widget(g_panel_log, 280, 22);
  animate_widget(g_btn_start, 340, 36);
  animate_widget(g_btn_bridge, 380, 36);
}

void refresh_ui() {
  AppSnapshot s;
  snapshot_state(s);

  String wifi_line = s.wifi;
  if (wifi_line.startsWith("WiFi: ") && !wifi_line.startsWith("WiFi: OFF") && !wifi_line.startsWith("WiFi: laczenie")) {
    wifi_line = "WiFi: OK  " + wifi_line;
  }

  if (wifi_line != g_ui_last_wifi && g_lbl_wifi) {
    g_ui_last_wifi = wifi_line;
    lv_label_set_text(g_lbl_wifi, wifi_line.c_str());
  }

  if (s.status != g_ui_last_status && g_lbl_status) {
    g_ui_last_status = s.status;
    lv_label_set_text(g_lbl_status, s.status.c_str());
  }

  if ((s.done != g_ui_last_done || s.total != g_ui_last_total) && g_lbl_progress) {
    g_ui_last_done = s.done;
    g_ui_last_total = s.total;
    String p = String("Postep: ") + s.done + "/" + s.total;
    lv_label_set_text(g_lbl_progress, p.c_str());
  }

  static unsigned long last_log_refresh = 0;
  unsigned long now = millis();
  if (s.log != g_ui_last_log && g_lbl_log && (now - last_log_refresh) >= 120) {
    last_log_refresh = now;
    g_ui_last_log = s.log;

    String recent = extract_recent_lines(s.log, 24);
    if (!recent.length()) recent = "Czekam na logi...\n";
    lv_label_set_text(g_lbl_log, recent.c_str());
    if (g_panel_log) lv_obj_scroll_to_y(g_panel_log, LV_COORD_MAX, LV_ANIM_OFF);

    String ips = collect_discovered_ips(s.log);
    if (ips != g_ui_last_ips && g_lbl_ips) {
      g_ui_last_ips = ips;
      if (!ips.length()) {
        lv_label_set_text(g_lbl_ips, "Brak wykrytych IP.");
      } else {
        int cnt = count_lines(ips);
        String txt = String("Liczba hostow: ") + cnt + "\n\n" + ips;
        lv_label_set_text(g_lbl_ips, txt.c_str());
      }
    }
  }

  update_algorithm_view(s);

  if (g_btn_start) {
    if (s.running) lv_obj_add_state(g_btn_start, LV_STATE_DISABLED);
    else lv_obj_clear_state(g_btn_start, LV_STATE_DISABLED);
  }
  if (g_btn_bridge) {
    if (s.running) lv_obj_add_state(g_btn_bridge, LV_STATE_DISABLED);
    else lv_obj_clear_state(g_btn_bridge, LV_STATE_DISABLED);
  }

  static unsigned long last_wifi_refresh = 0;
  if ((now - last_wifi_refresh) > 1200) {
    last_wifi_refresh = now;
    if (WiFi.status() == WL_CONNECTED) set_wifi_text(String("WiFi: ") + WiFi.localIP().toString());
    else if (s.running) set_wifi_text("WiFi: laczenie...");
  }
}
