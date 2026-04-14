#ifndef SENSOR_H
#define SENSOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

void dht_start_task(UBaseType_t uxPriority);
bool sensor_get_status(char *status, size_t status_len, float *humidity, float *temperature, uint32_t *last_read_ms);

#endif
