#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include "freertos/FreeRTOS.h"
#include <stdbool.h>

typedef enum {
    LED_MODE_BOOT = 0,
    LED_MODE_WIFI_CONNECTING,
    LED_MODE_WIFI_CONNECTED,
    LED_MODE_ERROR,
    LED_MODE_OFF,
} led_mode_t;

void led_manager_start_task(UBaseType_t uxPriority);
void led_manager_set_mode(led_mode_t mode);
void led_manager_pulse(void);

#endif
