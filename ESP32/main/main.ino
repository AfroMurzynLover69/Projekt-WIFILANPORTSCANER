// Single-file version: app state, hardware, UI, serial commands, scanner, setup/loop.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
#include <esp_io_expander.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#define LV_CONF_PATH "lv_conf.h"
#include <lvgl.h>
#include "konfiguracja.h"
#include "gui.h"
#include "scan_log_sd.h"

using namespace esp_panel::drivers;

// App state
struct AppSnapshot {
  String wifi;
  String status;
  String log;
  uint32_t done;
  uint32_t total;
  bool running;
};

static SemaphoreHandle_t g_lock = nullptr;

TaskHandle_t g_scan_task = nullptr;
bool g_scan_running = false;

static String g_wifi_text = "WiFi: OFF";
static String g_status_text = "Status: gotowy";
static String g_log_text = "Log: start\n";
static uint32_t g_progress_done = 0;
static uint32_t g_progress_total = 0;
static uint32_t g_port_scan_start = SCAN_PORT_START;
static uint32_t g_port_scan_end = SCAN_PORT_END;

void init_app_state() {
  if (!g_lock) g_lock = xSemaphoreCreateMutex();
}

void lock_state() {
  if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY);
}

void unlock_state() {
  if (g_lock) xSemaphoreGive(g_lock);
}

void set_status(const String &s) {
  lock_state();
  g_status_text = s;
  unlock_state();
}

void set_wifi_text(const String &s) {
  lock_state();
  g_wifi_text = s;
  unlock_state();
}

void set_progress(uint32_t done, uint32_t total) {
  lock_state();
  g_progress_done = done;
  g_progress_total = total;
  unlock_state();
}

void append_log(const String &line) {
  lock_state();
  String ln = line;
  if (ln.length() > 180) {
    ln = ln.substring(0, 180);
    ln += "...";
  }
  g_log_text += ln;
  g_log_text += "\n";
  if (g_log_text.length() > 6000) {
    g_log_text.remove(0, g_log_text.length() - 4500);
  }
  unlock_state();
}

void set_port_scan_range(uint32_t start, uint32_t end) {
  lock_state();
  g_port_scan_start = start;
  g_port_scan_end = end;
  unlock_state();
}

void set_port_profile_legacy() {
  set_port_scan_range(0, 4096);
}

void set_port_profile_full() {
  set_port_scan_range(SCAN_PORT_START, SCAN_PORT_END);
}

void get_port_scan_range(uint32_t &start, uint32_t &end) {
  lock_state();
  start = g_port_scan_start;
  end = g_port_scan_end;
  unlock_state();
}

bool is_port_profile_legacy() {
  lock_state();
  bool legacy = (g_port_scan_start == 0 && g_port_scan_end == 4096);
  unlock_state();
  return legacy;
}

void mark_scan_started() {
  lock_state();
  g_scan_running = true;
  g_progress_done = 0;
  g_progress_total = 0;
  unlock_state();
}

void mark_scan_finished() {
  lock_state();
  g_scan_running = false;
  g_scan_task = nullptr;
  unlock_state();
}

bool is_scan_busy() {
  lock_state();
  bool busy = g_scan_running || (g_scan_task != nullptr);
  unlock_state();
  return busy;
}

void snapshot_state(AppSnapshot &out) {
  lock_state();
  out.wifi = g_wifi_text;
  out.status = g_status_text;
  out.log = g_log_text;
  out.done = g_progress_done;
  out.total = g_progress_total;
  out.running = g_scan_running;
  unlock_state();
}

String get_log_snapshot() {
  lock_state();
  String out = g_log_text;
  unlock_state();
  return out;
}

uint32_t ip_to_u32(const IPAddress &ip) {
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
}

IPAddress u32_to_ip(uint32_t v) {
  return IPAddress((uint8_t)((v >> 24) & 0xFF), (uint8_t)((v >> 16) & 0xFF),
                   (uint8_t)((v >> 8) & 0xFF), (uint8_t)(v & 0xFF));
}

// Forward declaration used by UI/serial.
bool start_scan_request(const char *source);
const char *scan_mode_name();
void serial_pump();

void apply_wifi_link_limits() {
#if WIFI_USE_HT20_BANDWIDTH
  (void)WiFi.STA.bandwidth(WIFI_BW_HT20);
#endif
#if WIFI_FORCE_11B_PROTOCOL
  (void)esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);
#endif
}

static unsigned long g_wifi_retry_ts = 0;

static void wifi_bootstrap_start() {
  WiFi.mode(WIFI_STA);
  apply_wifi_link_limits();
  WiFi.setSleep(false);
#ifdef WIFI_POWER_8_5dBm
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  set_wifi_text("WiFi: laczenie...");
  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
  g_wifi_retry_ts = millis();
}

static void wifi_bootstrap_pump() {
  if (is_scan_busy()) return;
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if ((now - g_wifi_retry_ts) < 8000) return;

  g_wifi_retry_ts = now;
  set_wifi_text("WiFi: laczenie...");
  WiFi.disconnect(false, false);
  apply_wifi_link_limits();
  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);
}

// Hardware
static esp_expander::CH422G *g_expander = nullptr;
static LCD *g_lcd = nullptr;
static Touch *g_touch = nullptr;
static lv_display_t *g_disp = nullptr;
static lv_indev_t *g_indev = nullptr;
static lv_color_t *g_buf1 = nullptr;
static lv_color_t *g_buf2 = nullptr;
static bool g_use_hw_framebuffer_swap = false;
static unsigned long g_t_lv = 0;
static TaskHandle_t g_ui_task = nullptr;
static TaskHandle_t g_sys_task = nullptr;

static bool init_expander() {
  g_expander = new esp_expander::CH422G(PIN_I2C_SCL, PIN_I2C_SDA, CH422G_I2C_ADDR);
  return g_expander && g_expander->init() && g_expander->begin();
}

static LCD *create_lcd() {
  BusRGB *bus = new BusRGB(PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6,
                           PIN_LCD_D7, PIN_LCD_D8, PIN_LCD_D9, PIN_LCD_D10, PIN_LCD_D11, PIN_LCD_D12, PIN_LCD_D13,
                           PIN_LCD_D14, PIN_LCD_D15, PIN_LCD_HSYNC, PIN_LCD_VSYNC, PIN_LCD_PCLK, PIN_LCD_DE,
                           PIN_LCD_DISP, LCD_PCLK_HZ, LCD_WIDTH, LCD_HEIGHT, LCD_HPW, LCD_HBP, LCD_HFP, LCD_VPW,
                           LCD_VBP, LCD_VFP);
  return new LCD_ST7262(bus, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BITS, -1);
}

static void reset_touch_for_address(uint8_t addr) {
  pinMode(PIN_TOUCH_INT, OUTPUT);
  digitalWrite(PIN_TOUCH_INT, (addr == TOUCH_I2C_ADDR_BACKUP) ? HIGH : LOW);
  g_expander->digitalWrite(CH422G_PIN_TP_RST, 0);
  delay(100);
  g_expander->digitalWrite(CH422G_PIN_TP_RST, 1);
  delay(200);
  pinMode(PIN_TOUCH_INT, INPUT);
  delay(10);
}

static bool try_init_touch_addr(uint8_t addr) {
  reset_touch_for_address(addr);
  auto *bus = new BusI2C(0, ESP_PANEL_TOUCH_I2C_CONTROL_PANEL_CONFIG_WITH_ADDR(GT911, addr));
  auto *touch = new TouchGT911(bus, LCD_WIDTH, LCD_HEIGHT, -1, -1);
  if (!bus || !touch) return false;
  if (!touch->init() || !touch->begin()) {
    delete touch;
    delete bus;
    return false;
  }
#if TOUCH_SWAP_XY
  touch->swapXY(true);
#endif
#if TOUCH_MIRROR_X
  touch->mirrorX(true);
#endif
#if TOUCH_MIRROR_Y
  touch->mirrorY(true);
#endif
  g_touch = touch;
  return true;
}

bool init_lcd_touch() {
  if (!init_expander()) return false;

  g_expander->multiPinMode(CH422G_PIN_TP_RST | CH422G_PIN_LCD_BL | CH422G_PIN_LCD_RST | CH422G_PIN_SD_CS |
                               CH422G_PIN_USB_SEL,
                           OUTPUT);
  g_expander->multiDigitalWrite(CH422G_PIN_TP_RST | CH422G_PIN_LCD_BL | CH422G_PIN_LCD_RST, HIGH);
  g_expander->digitalWrite(CH422G_PIN_USB_SEL, LOW);
  g_expander->digitalWrite(CH422G_PIN_SD_CS, HIGH);

  g_expander->digitalWrite(CH422G_PIN_LCD_BL, LOW);
  g_expander->digitalWrite(CH422G_PIN_LCD_RST, LOW);
  delay(20);
  g_expander->digitalWrite(CH422G_PIN_LCD_RST, HIGH);
  delay(80);
  g_expander->digitalWrite(CH422G_PIN_LCD_BL, HIGH);

  g_lcd = create_lcd();
  if (!g_lcd) return false;

  auto *bus = static_cast<BusRGB *>(g_lcd->getBus());
  bus->configRGB_FrameBufferNumber(LCD_FRAMEBUFFER_COUNT);
  bus->configRGB_BounceBufferSize(LCD_BOUNCE_BUFFER_SIZE);
  bus->configRGB_TimingFlags(false, false, false, (LCD_PCLK_ACTIVE_NEG != 0), false);

  g_lcd->init();
  g_lcd->reset();
  if (!g_lcd->begin()) return false;
  if (g_lcd->getBasicAttributes().basic_bus_spec.isFunctionValid(LCD::BasicBusSpecification::FUNC_DISPLAY_ON_OFF)) {
    g_lcd->setDisplayOnOff(true);
  }

#if ENABLE_TOUCH
  return try_init_touch_addr(TOUCH_I2C_ADDR_PRIMARY) || try_init_touch_addr(TOUCH_I2C_ADDR_BACKUP);
#else
  return true;
#endif
}

static void lv_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  if (!g_lcd) {
    lv_display_flush_ready(d);
    return;
  }

  if (g_use_hw_framebuffer_swap) {
    if (lv_display_flush_is_last(d)) {
      g_lcd->switchFrameBufferTo(px);
    }
    lv_display_flush_ready(d);
    return;
  }

  g_lcd->drawBitmap(a->x1, a->y1, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1, reinterpret_cast<const uint8_t *>(px));
  lv_display_flush_ready(d);
}

static void lv_touch(lv_indev_t *i, lv_indev_data_t *data) {
  (void)i;
  if (!g_touch) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  TouchPoint p[1];
  int n = g_touch->readPoints(p, 1, 0);
  if (n > 0) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = p[0].x;
    data->point.y = p[0].y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

bool init_lvgl() {
  lv_init();
  uint32_t px = (uint32_t)LCD_WIDTH * LCD_HEIGHT;
  g_buf1 = g_lcd ? static_cast<lv_color_t *>(g_lcd->getFrameBufferByIndex(0)) : nullptr;
  g_buf2 = g_lcd ? static_cast<lv_color_t *>(g_lcd->getFrameBufferByIndex(1)) : nullptr;
  g_use_hw_framebuffer_swap = (g_buf1 != nullptr && g_buf2 != nullptr);

  if (!g_use_hw_framebuffer_swap) {
    const uint32_t fallback_lines = 24;
    px = (uint32_t)LCD_WIDTH * fallback_lines;
    g_buf1 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!g_buf1) g_buf1 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_buf2 = nullptr;
    if (!g_buf1) return false;
  }

  g_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  if (!g_disp) return false;

  lv_display_set_buffers(g_disp, g_buf1, g_buf2, px * sizeof(lv_color_t),
                         g_use_hw_framebuffer_swap ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(g_disp, lv_flush);

  if (g_touch) {
    g_indev = lv_indev_create();
    lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_indev, lv_touch);
  }

  g_t_lv = millis();
  return true;
}

void lv_pump() {
  unsigned long now = millis();
  unsigned long diff = now - g_t_lv;
  if (!diff) return;
  lv_tick_inc(diff);
  g_t_lv = now;
  lv_timer_handler();
}

static void ui_task_main(void *arg) {
  (void)arg;
  uint32_t busy_counter = 0;
  while (true) {
    lv_pump();
    bool busy = is_scan_busy();
    if (!busy || ((busy_counter++ % UI_REFRESH_WHEN_BUSY_EVERY) == 0)) {
      refresh_ui();
    }
    vTaskDelay(pdMS_TO_TICKS(busy ? UI_BUSY_LOOP_DELAY_MS : UI_IDLE_LOOP_DELAY_MS));
  }
}

static void sys_task_main(void *arg) {
  (void)arg;
  while (true) {
    serial_pump();
    wifi_bootstrap_pump();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// UI moved to gui.cpp

// Serial commands
void print_serial_help() {
  Serial.println("KOMENDY:");
  Serial.println("  HELP");
  Serial.println("  STATUS");
  Serial.println("  START   (local scan)");
  Serial.println("  SD      (status SD)");
}

static void serial_status() {
  uint32_t port_start = 0, port_end = 0;
  get_port_scan_range(port_start, port_end);
  Serial.println(is_scan_busy() ? "SCAN: RUNNING" : "SCAN: IDLE");
  Serial.println(String("MODE: ") + scan_mode_name() + " + PORTS " + port_start + "-" + port_end);
  Serial.println(scan_log_sd_status_text());
  String last_file = scan_log_sd_last_file();
  if (last_file.length()) Serial.println(String("SD_LAST_FILE: ") + last_file);
}

static void process_serial_cmd(String line) {
  line.trim();
  if (!line.length()) return;

  String upper = line;
  upper.toUpperCase();

  if (upper == "HELP") {
    print_serial_help();
    return;
  }
  if (upper == "STATUS") {
    serial_status();
    return;
  }
  if (upper == "START") {
    (void)start_scan_request("SERIAL: start scan");
    return;
  }
  if (upper == "SD") {
    Serial.println(scan_log_sd_status_text());
    String last_file = scan_log_sd_last_file();
    if (last_file.length()) Serial.println(String("SD_LAST_FILE: ") + last_file);
    return;
  }

  Serial.println("ERR nieznana komenda. Wpisz HELP");
}

void serial_setup() {
  Serial.begin(115200);
  delay(50);
  print_serial_help();
}

void serial_pump() {
  static String buf;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      process_serial_cmd(buf);
      buf = "";
      continue;
    }
    if (buf.length() < 120) buf += c;
  }
}

// Scanner moved to scan.cpp

// Main
void setup() {
  init_app_state();
  serial_setup();
  wifi_bootstrap_start();

  if (!init_lcd_touch()) {
    while (true) delay(500);
  }

  if (scan_log_sd_init(g_expander)) {
    append_log("SD: karta gotowa");
  } else {
    append_log("SD: init fail (brak karty/mount)");
  }

  if (!init_lvgl()) {
    while (true) delay(500);
  }

  create_gui();

  if (xTaskCreatePinnedToCore(ui_task_main, "ui_task", 8192, nullptr, 2, &g_ui_task, 1) != pdPASS) {
    append_log("TASK: ui_task create fail");
  }
  if (xTaskCreatePinnedToCore(sys_task_main, "sys_task", 4096, nullptr, 1, &g_sys_task, 0) != pdPASS) {
    append_log("TASK: sys_task create fail");
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
