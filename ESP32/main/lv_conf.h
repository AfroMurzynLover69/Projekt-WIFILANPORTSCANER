#ifndef LV_CONF_H
#define LV_CONF_H

/*
 * Projekt dostarcza lokalne lv_conf.h, zeby build Arduino nie zalezyl
 * od recznego kopiowania konfiguracji obok zainstalowanej biblioteki lvgl.
 * Reszte opcji dopelnia lv_conf_internal.h z samej biblioteki.
 */

#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_FREERTOS

/* Bigger fonts used by custom GUI */
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1

/* Disable built-in themes; GUI styles are set explicitly in gui.cpp. */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#endif
