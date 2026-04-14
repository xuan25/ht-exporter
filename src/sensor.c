#include "sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "DHT.h"
#include "led_manager.h"

static const char *TAG = "sensor";
static TaskHandle_t s_sensor_task = NULL;
static float s_last_humidity = 0.0f;
static float s_last_temperature = 0.0f;
static uint32_t s_last_read_ms = 0;

static void DHT_task(void *pvParameter)
{
    esp_rom_gpio_pad_select_gpio(GPIO_NUM_0);
    setDHTgpio(GPIO_NUM_0);
    ESP_LOGI(TAG, "DHT task started");

    while (1)
    {
        int ret = readDHT();

        errorHandler(ret);

        if (ret == DHT_OK) {
            s_last_humidity = getHumidity();
            s_last_temperature = getTemperature();
            s_last_read_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            led_manager_pulse();
        }

        // -- wait at least 2 sec before reading again ------------
        // The interval of whole process must be beyond 2 seconds !!
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void dht_start_task(UBaseType_t uxPriority)
{
    xTaskCreate(&DHT_task, "DHT_task", 4096, NULL, uxPriority, &s_sensor_task);
}

bool sensor_get_status(char *status, size_t status_len, float *humidity, float *temperature, uint32_t *last_read_ms)
{
    if (status == NULL || status_len == 0) {
        return false;
    }

    if (s_sensor_task == NULL) {
        snprintf(status, status_len, "not started");
        return false;
    }

    eTaskState state = eTaskGetState(s_sensor_task);
    const char *state_str = "unknown";
    switch (state) {
        case eReady:
            state_str = "ready";
            break;
        case eRunning:
            state_str = "running";
            break;
        case eBlocked:
            state_str = "blocked";
            break;
        case eSuspended:
            state_str = "suspended";
            break;
        case eDeleted:
            state_str = "deleted";
            break;
        default:
            break;
    }

    snprintf(status, status_len, "%s", state_str);
    if (humidity) {
        *humidity = s_last_humidity;
    }
    if (temperature) {
        *temperature = s_last_temperature;
    }
    if (last_read_ms) {
        *last_read_ms = s_last_read_ms;
    }

    return true;
}
