#ifndef LV_CONF_H
#define LV_CONF_H

/*
 * Projekt dostarcza lokalne lv_conf.h, zeby build Arduino nie zalezyl
 * od recznego kopiowania konfiguracji obok zainstalowanej biblioteki lvgl.
 * Reszte opcji dopelnia lv_conf_internal.h z samej biblioteki.
 */

#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_FREERTOS

#endif
