#ifndef MQTT_H_
#define MQTT_H_

#include "freertos/task.h"

#define QOS0 0
#define QOS1 1
#define QOS2 2

#define NO_RETAIN 0
#define RETAIN 1

extern TaskHandle_t mqtt_task_handle;

void app_mqtt_init(void);
void app_mqtt_start(void);
void app_mqtt_stop(void);
void app_mqtt_task(void *pvParameters);

#endif