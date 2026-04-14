#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

void webserver_start_task(EventGroupHandle_t app_event_group, EventBits_t handlers_ready_bit, UBaseType_t uxPriority);

#endif
