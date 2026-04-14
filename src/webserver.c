#include "webserver.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "DHT.h"
#include "led_manager.h"

static const char *TAG = "webserver";
static httpd_handle_t s_server = NULL;
static EventGroupHandle_t s_app_event_group = NULL;
static EventBits_t s_handlers_ready_bit = 0;

static esp_err_t root_get_handler(httpd_req_t *req){
    float humidity = getHumidity();
    float temperature = getTemperature();
    char body[384];
    int len = snprintf(
        body,
        sizeof(body),
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>HT Monitor</title></head><body>"
        "<h1>HT Monitor</h1>"
        "<p>Humidity: %.1f%%</p>"
        "<p>Temperature: %.1f C</p>"
        "<p><a href=\"/metrics\">/metrics</a></p>"
        "</body></html>",
        humidity,
        temperature
    );
    if (len < 0 || len >= (int)sizeof(body)) {
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, body, len);
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static esp_err_t metrics_get_handler(httpd_req_t *req)
{
    float humidity = getHumidity();
    float temperature = getTemperature();
    httpd_resp_set_type(req, "text/plain; version=0.0.4");

    if (httpd_resp_sendstr_chunk(req, "# HELP dht_humidity_percent Relative humidity in percent.\n") != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(req, "# TYPE dht_humidity_percent gauge\n") != ESP_OK) {
        return ESP_FAIL;
    }

    char line[64];
    int len = snprintf(line, sizeof(line), "dht_humidity_percent %.1f\n", humidity);
    if (len < 0 || len >= (int)sizeof(line)) {
        return ESP_FAIL;
    }
    if (httpd_resp_send_chunk(req, line, len) != ESP_OK) {
        return ESP_FAIL;
    }

    if (httpd_resp_sendstr_chunk(req, "# HELP dht_temperature_celsius Temperature in Celsius.\n") != ESP_OK) {
        return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(req, "# TYPE dht_temperature_celsius gauge\n") != ESP_OK) {
        return ESP_FAIL;
    }

    len = snprintf(line, sizeof(line), "dht_temperature_celsius %.1f\n", temperature);
    if (len < 0 || len >= (int)sizeof(line)) {
        return ESP_FAIL;
    }
    if (httpd_resp_send_chunk(req, line, len) != ESP_OK) {
        return ESP_FAIL;
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

static const httpd_uri_t metrics = {
    .uri       = "/metrics",
    .method    = HTTP_GET,
    .handler   = metrics_get_handler,
    .user_ctx  = NULL
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 9100;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: %d", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &metrics);
        return server;
    }

    ESP_LOGE(TAG, "Failed to start server");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (server && *server) {
        ESP_LOGI(TAG, "Stopping server");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop server");
        }
    }

    led_manager_set_mode(LED_MODE_WIFI_CONNECTING);
    ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting");
    esp_wifi_connect();
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (server && *server == NULL) {
        if (event_data) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        }
        led_manager_set_mode(LED_MODE_WIFI_CONNECTED);
        ESP_LOGI(TAG, "Starting server");
        *server = start_webserver();
    }
}

static void wifi_start_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    led_manager_set_mode(LED_MODE_WIFI_CONNECTING);
    ESP_LOGI(TAG, "Wi-Fi started, connecting");
    esp_wifi_connect();
}

static void webserver_task(void *pvParameter)
{
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &s_server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &s_server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &wifi_start_handler, NULL));

    xEventGroupSetBits(s_app_event_group, s_handlers_ready_bit);
    vTaskDelete(NULL);
}

void webserver_start_task(EventGroupHandle_t app_event_group, EventBits_t handlers_ready_bit, UBaseType_t uxPriority)
{
    if (app_event_group == NULL) {
        ESP_LOGE(TAG, "Event group not initialized");
        return;
    }

    s_app_event_group = app_event_group;
    s_handlers_ready_bit = handlers_ready_bit;
    xTaskCreate(&webserver_task, "webserver_task", 4096, NULL, uxPriority, NULL);
}
