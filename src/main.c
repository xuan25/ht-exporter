#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_system.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"

#include <unistd.h>
#include "sensor.h"
#include "cli.h"
#include "led_manager.h"
#include "webserver.h"
#include "wifi_manager.h"



static const char *TAG = "app";
static EventGroupHandle_t s_app_event_group = NULL;

#define HANDLERS_READY_BIT BIT0


void app_main()
{
    // Set log level
    esp_log_level_set("*", ESP_LOG_INFO);

    // Startup delay
    int startup_delay = 5;
    for (int i = startup_delay; i > 0; i--) {
        ESP_LOGI(TAG, "Starting in %d seconds", i);
        sleep(1);
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_app_event_group = xEventGroupCreate();
    if (s_app_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    // LED manager
    led_manager_start_task(3);
    led_manager_set_mode(LED_MODE_BOOT);
    // DHT
    dht_start_task(7);
    // HTTP server
    webserver_start_task(s_app_event_group, HANDLERS_READY_BIT, 6);
    // Wi-Fi tasks
    wifi_manager_start_task(s_app_event_group, HANDLERS_READY_BIT, 5);
    // Serial CLI
    cli_start_task(4);

    ESP_LOGI(TAG, "System started");
}
