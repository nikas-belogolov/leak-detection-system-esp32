#ifndef COMMON_H
#define COMMON_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const int WIFI_CONNECTED_BIT = BIT0;
static const int TIME_SYNCED_BIT = BIT1;

extern EventGroupHandle_t app_event_group;

const char* app_get_device_id();
void app_device_id_init();

#endif