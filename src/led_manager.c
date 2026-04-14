#include "led_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led";

#ifndef LED_GPIO
#define LED_GPIO GPIO_NUM_15
#endif

#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

static TaskHandle_t s_led_task = NULL;
static volatile led_mode_t s_mode = LED_MODE_BOOT;
static volatile bool s_pulse = false;

static void led_write(bool on)
{
    int level = on ? 1 : 0;
#if LED_ACTIVE_LOW
    level = !level;
#endif
    gpio_set_level(LED_GPIO, level);
}

static void led_delay(uint32_t ms)
{
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
}

static void led_task(void *pvParameter)
{
    (void)pvParameter;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    led_write(false);
    ESP_LOGI(TAG, "LED manager started (GPIO=%d, active_%s)", LED_GPIO, LED_ACTIVE_LOW ? "low" : "high");

    while (1) {
        if (s_pulse) {
            s_pulse = false;
            led_write(true);
            led_delay(60);
            led_write(false);
            led_delay(60);
        }

        led_mode_t mode = s_mode;
        switch (mode) {
            case LED_MODE_OFF:
                led_write(false);
                led_delay(1000);
                break;
            case LED_MODE_WIFI_CONNECTED:
                led_write(true);
                led_delay(1000);
                break;
            case LED_MODE_WIFI_CONNECTING:
                led_write(true);
                led_delay(200);
                if (s_mode != mode) {
                    break;
                }
                led_write(false);
                led_delay(200);
                break;
            case LED_MODE_BOOT:
                led_write(true);
                led_delay(500);
                if (s_mode != mode) {
                    break;
                }
                led_write(false);
                led_delay(500);
                break;
            case LED_MODE_ERROR:
            default:
                led_write(true);
                led_delay(150);
                if (s_mode != mode) {
                    break;
                }
                led_write(false);
                led_delay(150);
                if (s_mode != mode) {
                    break;
                }
                led_write(true);
                led_delay(150);
                if (s_mode != mode) {
                    break;
                }
                led_write(false);
                led_delay(700);
                break;
        }
    }
}

void led_manager_start_task(UBaseType_t uxPriority)
{
    if (s_led_task != NULL) {
        return;
    }

    xTaskCreate(&led_task, "led_task", 2048, NULL, uxPriority, &s_led_task);
}

void led_manager_set_mode(led_mode_t mode)
{
    s_mode = mode;
    if (s_led_task != NULL) {
        xTaskNotifyGive(s_led_task);
    }
}

void led_manager_pulse(void)
{
    s_pulse = true;
    if (s_led_task != NULL) {
        xTaskNotifyGive(s_led_task);
    }
}
