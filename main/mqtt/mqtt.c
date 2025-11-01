#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"

#include "esp_log.h"

#include "wifi_provisioning/manager.h"

#include <math.h>

#include "pb_encode.h"
#include "sample_batch.pb.h"
#include "sensors.h"
#include "common.h"
#include "prov.h"
#include "mqtt.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "ulp.h"
#include "ulp_main.h"
#include "esp_sleep.h"

static const char *TAG = "MQTT";

esp_mqtt_client_handle_t client;
TaskHandle_t mqtt_task_handle;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        if (mqtt_task_handle == NULL)
        {
            xTaskCreate(app_mqtt_task, "mqtt_task", 4096, NULL, 5, &mqtt_task_handle);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        if (mqtt_task_handle != NULL)
        {
            vTaskDelete(mqtt_task_handle);
            mqtt_task_handle = NULL;
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR: %d", event->error_handle->error_type);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            ESP_LOGI(TAG, "Failed to connect to MQTT broker.");
            ESP_LOGI(TAG, "Reconnecting to MQTT broker...");
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void app_mqtt_init(void)
{

    const char *device_id = app_get_device_id();

    const esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URL,
        .credentials = {
            .username = device_id,
            .client_id = device_id
        }
    };

    client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
}

void app_mqtt_start(void)
{
    esp_mqtt_client_start(client);
}

void app_mqtt_stop(void)
{
    esp_mqtt_client_stop(client);
}

bool should_stay_awake(SampleBatch *batch)
{
    for (int i = 0; i < SAMPLE_BATCH_SIZE; ++i)
    {
        if (
            batch->samples[i].flow != 0 ||
            batch->samples[i].pressure > PRESSURE_MIN_VALUE)
            return true;
    }

    return false;
}

void app_mqtt_task(void *pvParameters)
{
    SampleBatch batch = SampleBatch_init_zero;
    uint8_t buffer[SampleBatch_size];

    const char *device_id = app_get_device_id();

    char topic[256];
    snprintf(topic, sizeof(topic), "%s%s%s", "devices/", device_id, "/flow_pressure_data");

    while (1)
    {

        app_sensors_read(&batch.samples[batch.samples_count]);
        batch.samples_count++;

        if (batch.samples_count == SAMPLE_BATCH_SIZE)
        {
            if (!should_stay_awake(&batch))
            {
                ESP_LOGI(TAG, "Deep sleep due to inactivity");

#if CONFIG_IDF_TARGET_ESP32
                rtc_gpio_isolate(FLOW_SENSOR_PIN);
#endif

                ESP_ERROR_CHECK(ulp_run(&ulp_entry - RTC_SLOW_MEM));
                esp_deep_sleep_start();
            }

            pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
            pb_encode(&stream, SampleBatch_fields, &batch);

            ESP_LOGI(TAG, "Sending %d bytes", stream.bytes_written);
            ESP_LOGI(TAG, "Sending %d samples", batch.samples_count);

            esp_mqtt_client_publish(client, topic, (const char *)buffer, stream.bytes_written, QOS1, NO_RETAIN);
            batch.samples_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(MEASUREMENT_INTERVAL_MS));
    }
}