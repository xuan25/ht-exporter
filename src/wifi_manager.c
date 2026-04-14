#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "led_manager.h"

static const char *TAG = "wifi";
static EventGroupHandle_t s_app_event_group = NULL;
static EventBits_t s_handlers_ready_bit = 0;
static esp_netif_t *s_sta_netif = NULL;

#define WIFI_NAMESPACE "wifi"
#define WIFI_KEY_SSID "ssid"
#define WIFI_KEY_PASS "pass"
#define WIFI_KEY_STATIC_IP "static_ip"
#define WIFI_KEY_STATIC_GW "static_gw"
#define WIFI_KEY_STATIC_MASK "static_mask"

static esp_err_t ensure_netif(void)
{
    if (s_sta_netif != NULL) {
        return ESP_OK;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    return (s_sta_netif != NULL) ? ESP_OK : ESP_FAIL;
}

static bool parse_ip4(const char *str, esp_ip4_addr_t *out)
{
    if (str == NULL || out == NULL) {
        return false;
    }
    ip4_addr_t tmp = {0};
    if (ip4addr_aton(str, &tmp) == 0) {
        return false;
    }
    memcpy(out, &tmp, sizeof(*out));
    return true;
}

static esp_err_t apply_static_ip_from_nvs(void)
{
    if (s_sta_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    char ip_str[16] = {0};
    char gw_str[16] = {0};
    char mask_str[16] = {0};
    size_t ip_len = sizeof(ip_str);
    size_t gw_len = sizeof(gw_str);
    size_t mask_len = sizeof(mask_str);

    err = nvs_get_str(nvs_handle, WIFI_KEY_STATIC_IP, ip_str, &ip_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, WIFI_KEY_STATIC_GW, gw_str, &gw_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, WIFI_KEY_STATIC_MASK, mask_str, &mask_len);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    esp_netif_ip_info_t ip_info = {0};
    if (!parse_ip4(ip_str, &ip_info.ip) || !parse_ip4(gw_str, &ip_info.gw) || !parse_ip4(mask_str, &ip_info.netmask)) {
        return ESP_ERR_INVALID_ARG;
    }

    err = esp_netif_dhcpc_stop(s_sta_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return err;
    }

    return esp_netif_set_ip_info(s_sta_netif, &ip_info);
}

static esp_err_t load_default_credentials(wifi_config_t *wifi_config)
{
#if defined(WIFI_DEFAULT_SSID) && defined(WIFI_DEFAULT_PASS)
    if (wifi_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(wifi_config, 0, sizeof(*wifi_config));
    strncpy((char *)wifi_config->sta.ssid, WIFI_DEFAULT_SSID, sizeof(wifi_config->sta.ssid) - 1);
    strncpy((char *)wifi_config->sta.password, WIFI_DEFAULT_PASS, sizeof(wifi_config->sta.password) - 1);
    return ESP_OK;
#else
    (void)wifi_config;
    return ESP_ERR_NOT_FOUND;
#endif
}

static esp_err_t load_credentials(wifi_config_t *wifi_config)
{
    if (wifi_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return load_default_credentials(wifi_config);
    }

    memset(wifi_config, 0, sizeof(*wifi_config));

    size_t ssid_len = sizeof(wifi_config->sta.ssid);
    size_t pass_len = sizeof(wifi_config->sta.password);

    err = nvs_get_str(nvs_handle, WIFI_KEY_SSID, (char *)wifi_config->sta.ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return load_default_credentials(wifi_config);
    }

    err = nvs_get_str(nvs_handle, WIFI_KEY_PASS, (char *)wifi_config->sta.password, &pass_len);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        return load_default_credentials(wifi_config);
    }

    return ESP_OK;
}

static void wifi_task(void *pvParameter)
{
    if (s_app_event_group == NULL) {
        ESP_LOGE(TAG, "Event group not initialized");
        vTaskDelete(NULL);
        return;
    }

    xEventGroupWaitBits(s_app_event_group, s_handlers_ready_bit, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "Initializing Wi-Fi");

    ESP_ERROR_CHECK(ensure_netif());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {0};
    esp_err_t err = load_credentials(&wifi_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No Wi-Fi credentials. Use CLI: wifi_set <ssid> <pass>");
        led_manager_set_mode(LED_MODE_ERROR);
        vTaskDelete(NULL);
        return;
    }
    
    esp_err_t ip_err = apply_static_ip_from_nvs();
    if (ip_err == ESP_OK) {
        ESP_LOGI(TAG, "Using static IP");
    } else if (ip_err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "Using DHCP");
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    vTaskDelete(NULL);
}

void wifi_manager_start_task(EventGroupHandle_t app_event_group, EventBits_t handlers_ready_bit, UBaseType_t uxPriority)
{
    s_app_event_group = app_event_group;
    s_handlers_ready_bit = handlers_ready_bit;
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, uxPriority, NULL);
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *pass)
{
    if (ssid == NULL || pass == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs_handle, WIFI_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, WIFI_KEY_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_manager_get_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    if (ssid == NULL || pass == NULL || ssid_len == 0 || pass_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        wifi_config_t wifi_config = {0};
        err = load_default_credentials(&wifi_config);
        if (err != ESP_OK) {
            return err;
        }
        strncpy(ssid, (const char *)wifi_config.sta.ssid, ssid_len - 1);
        strncpy(pass, (const char *)wifi_config.sta.password, pass_len - 1);
        ssid[ssid_len - 1] = '\0';
        pass[pass_len - 1] = '\0';
        return ESP_OK;
    }

    err = nvs_get_str(nvs_handle, WIFI_KEY_SSID, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, WIFI_KEY_PASS, pass, &pass_len);
    }

    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        wifi_config_t wifi_config = {0};
        err = load_default_credentials(&wifi_config);
        if (err != ESP_OK) {
            return err;
        }
        strncpy(ssid, (const char *)wifi_config.sta.ssid, ssid_len - 1);
        strncpy(pass, (const char *)wifi_config.sta.password, pass_len - 1);
        ssid[ssid_len - 1] = '\0';
        pass[pass_len - 1] = '\0';
        return ESP_OK;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_apply_credentials(void)
{
    wifi_config_t wifi_config = {0};
    esp_err_t err = load_credentials(&wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    err = ensure_netif();
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t ip_err = apply_static_ip_from_nvs();
    if (ip_err != ESP_OK && ip_err != ESP_ERR_NOT_FOUND) {
        return ip_err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err == ESP_OK) {
        return ESP_OK;
    }
#if defined(ESP_ERR_WIFI_NOT_STOPPED)
    if (err != ESP_ERR_WIFI_NOT_STOPPED) {
        return err;
    }
#else
    if (err != ESP_OK) {
        return err;
    }
#endif

    err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        esp_err_t start_err = esp_wifi_start();
        if (start_err == ESP_OK) {
            return ESP_OK;
        }
#if defined(ESP_ERR_WIFI_NOT_STOPPED)
        if (start_err != ESP_ERR_WIFI_NOT_STOPPED) {
            return start_err;
        }
#else
        if (start_err != ESP_OK) {
            return start_err;
        }
#endif
        err = esp_wifi_connect();
    }

    return err;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t err_ssid = nvs_erase_key(nvs_handle, WIFI_KEY_SSID);
    esp_err_t err_pass = nvs_erase_key(nvs_handle, WIFI_KEY_PASS);

    if (err_ssid == ESP_OK || err_pass == ESP_OK) {
        err = nvs_commit(nvs_handle);
    } else {
        err = (err_ssid != ESP_ERR_NOT_FOUND) ? err_ssid : err_pass;
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_manager_stop(void)
{
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        return err;
    }

    err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        led_manager_set_mode(LED_MODE_OFF);
        return ESP_OK;
    }

    if (err == ESP_OK) {
        led_manager_set_mode(LED_MODE_OFF);
    }
    return err;
}

esp_err_t wifi_manager_set_static_ip(const char *ip, const char *gw, const char *netmask)
{
    if (ip == NULL || gw == NULL || netmask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_ip4_addr_t ip_addr = {0};
    esp_ip4_addr_t gw_addr = {0};
    esp_ip4_addr_t mask_addr = {0};

    if (!parse_ip4(ip, &ip_addr) || !parse_ip4(gw, &gw_addr) || !parse_ip4(netmask, &mask_addr)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs_handle, WIFI_KEY_STATIC_IP, ip);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, WIFI_KEY_STATIC_GW, gw);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs_handle, WIFI_KEY_STATIC_MASK, netmask);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        return err;
    }

    if (s_sta_netif != NULL) {
        return apply_static_ip_from_nvs();
    }

    return ESP_OK;
}

esp_err_t wifi_manager_clear_static_ip(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t err_ip = nvs_erase_key(nvs_handle, WIFI_KEY_STATIC_IP);
    esp_err_t err_gw = nvs_erase_key(nvs_handle, WIFI_KEY_STATIC_GW);
    esp_err_t err_mask = nvs_erase_key(nvs_handle, WIFI_KEY_STATIC_MASK);

    if (err_ip == ESP_OK || err_gw == ESP_OK || err_mask == ESP_OK) {
        err = nvs_commit(nvs_handle);
    } else {
        err = (err_ip != ESP_ERR_NOT_FOUND) ? err_ip : (err_gw != ESP_ERR_NOT_FOUND ? err_gw : err_mask);
    }

    nvs_close(nvs_handle);

    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return err;
    }

    if (s_sta_netif != NULL) {
        esp_err_t dhcp_err = esp_netif_dhcpc_start(s_sta_netif);
        if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            return dhcp_err;
        }
    }

    return ESP_OK;
}
