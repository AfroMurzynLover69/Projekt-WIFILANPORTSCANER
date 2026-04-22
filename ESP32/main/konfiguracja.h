#pragma once

// Display and timing
#define LCD_WIDTH 1024
#define LCD_HEIGHT 600
#define LCD_PCLK_HZ (20 * 1000 * 1000)
#define LCD_HPW 30
#define LCD_HBP 145
#define LCD_HFP 170
#define LCD_VPW 2
#define LCD_VBP 23
#define LCD_VFP 12
#define LCD_PCLK_ACTIVE_NEG 1
#define LCD_COLOR_BITS 16
#define LCD_BOUNCE_BUFFER_SIZE (LCD_WIDTH * 20)
#define LCD_FRAMEBUFFER_COUNT 2

// ESP32-S3 RGB pins
#define PIN_LCD_HSYNC 46
#define PIN_LCD_VSYNC 3
#define PIN_LCD_DE 5
#define PIN_LCD_PCLK 7
#define PIN_LCD_DISP (-1)
#define PIN_LCD_D0 14
#define PIN_LCD_D1 38
#define PIN_LCD_D2 18
#define PIN_LCD_D3 17
#define PIN_LCD_D4 10
#define PIN_LCD_D5 39
#define PIN_LCD_D6 0
#define PIN_LCD_D7 45
#define PIN_LCD_D8 48
#define PIN_LCD_D9 47
#define PIN_LCD_D10 21
#define PIN_LCD_D11 1
#define PIN_LCD_D12 2
#define PIN_LCD_D13 42
#define PIN_LCD_D14 41
#define PIN_LCD_D15 40

// I2C and expander
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define CH422G_I2C_ADDR 0x20

#define CH422G_PIN_TP_RST 1
#define CH422G_PIN_LCD_BL 2
#define CH422G_PIN_LCD_RST 3
#define CH422G_PIN_SD_CS 4
#define CH422G_PIN_USB_SEL 5

// TF (SD) slot wiring
#define PIN_SD_CMD 11
#define PIN_SD_CLK 12
#define PIN_SD_D0 13

// SD logging settings
#define SCAN_SD_MOUNT_POINT "/sdcard"
#define SCAN_SD_MAX_OPEN_FILES 5
#define SCAN_SD_FREQ_HZ 20000000
#define SCAN_LOG_TZ "CET-1CEST,M3.5.0/2,M10.5.0/3"
#define SCAN_LOG_NTP_SERVER_1 "pool.ntp.org"
#define SCAN_LOG_NTP_SERVER_2 "time.nist.gov"
#define SCAN_LOG_NTP_SERVER_3 "time.google.com"

// Touch
#define PIN_TOUCH_INT 4
#define TOUCH_I2C_ADDR_PRIMARY 0x5D
#define TOUCH_I2C_ADDR_BACKUP 0x14
#define TOUCH_SWAP_XY 0
#define TOUCH_MIRROR_X 0
#define TOUCH_MIRROR_Y 0
#define ENABLE_TOUCH 1

// WiFi
#define DEFAULT_WIFI_SSID "10"
#define DEFAULT_WIFI_PASS "R4TAWQ76"

// Scan modes
#define SCAN_MODE_ARP_ONLY 0
#define SCAN_MODE_BCAST_THEN_ARP 1

// Active mode
#define SCAN_MODE SCAN_MODE_BCAST_THEN_ARP

// Scan timing
#define SCAN_WIFI_CONNECT_TIMEOUT_MS 10000UL
#define SCAN_BROADCAST_WAIT_MS 10000UL
#define SCAN_ARP_TOTAL_WAIT_MS 60000UL
#define SCAN_ARP_PER_HOST_DELAY_MS 2UL
#define SCAN_PORT_PACE_EVERY 8UL
#define SCAN_PORT_PACE_DELAY_MS 2UL

// UI scheduling (to reduce LCD/PSRAM contention during heavy WiFi work)
#define UI_IDLE_LOOP_DELAY_MS 5UL
#define UI_BUSY_LOOP_DELAY_MS 16UL
#define UI_REFRESH_WHEN_BUSY_EVERY 2UL

// Scan limits
#define SCAN_MAX_HOSTS 254UL

// Full TCP port scan (final step)
#define SCAN_PORT_START 1UL
#define SCAN_PORT_END 65535UL
#define SCAN_PORT_TIMEOUT_MS 35UL
#define SCAN_PORT_PROGRESS_EVERY 64UL

// WiFi stability knobs
#define WIFI_USE_HT20_BANDWIDTH 1
#define WIFI_FORCE_11B_PROTOCOL 0
