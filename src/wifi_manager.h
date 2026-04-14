#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

void wifi_manager_start_task(EventGroupHandle_t app_event_group, EventBits_t handlers_ready_bit, UBaseType_t uxPriority);
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *pass);
esp_err_t wifi_manager_get_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t wifi_manager_apply_credentials(void);
esp_err_t wifi_manager_clear_credentials(void);
esp_err_t wifi_manager_stop(void);
esp_err_t wifi_manager_set_static_ip(const char *ip, const char *gw, const char *netmask);
esp_err_t wifi_manager_clear_static_ip(void);

#endif
