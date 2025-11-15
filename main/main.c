#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#include "sdkconfig.h"

#include "common.h"
#include "prov.h"
#include "sntp.h"
#include "mqtt.h"
#include "sensors.h"

#include "esp_mac.h"
#include "esp_pm.h"

#include "esp_sleep.h"

#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc_periph.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "ulp.h"
#include "ulp_main.h"

#include "esp_system.h"

static const char *TAG = "app";

EventGroupHandle_t app_event_group = NULL;

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");

void init_ulp_program(void);

void nvs_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    // Initialize Power Management
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    // Initialize NVS
    nvs_init();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wakeup cause: %d", cause);

    if (cause != ESP_SLEEP_WAKEUP_ULP) {
        init_ulp_program();
        esp_wifi_restore();
    }        

    app_device_id_init();
    const char *device_id = app_get_device_id();
    ESP_LOGI(TAG, "Device Unique ID: %s, len: %d", device_id, strlen(device_id));

    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    app_event_group = xEventGroupCreate();
    if (app_event_group == NULL)
    {
        ESP_LOGE(TAG, "Failed to create event group!");
        return;
    }

    app_sntp_init();
    app_sensors_init();
    app_mqtt_init();
    app_wifi_init();
    app_prov_init();

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned)
    {
        app_prov_start();
    }
    else
    {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
        // app_prov_stop();
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM)); // for low power
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    /* Wait for Wi-Fi connection */
    xEventGroupWaitBits(app_event_group, WIFI_CONNECTED_BIT, true, true, portMAX_DELAY);

    ESP_LOGI(TAG, "Connected to wifi");

    app_sntp_start();

    xEventGroupWaitBits(app_event_group, TIME_SYNCED_BIT, true, true, portMAX_DELAY);

    app_mqtt_start();
}

void init_ulp_program(void)
{

    ESP_ERROR_CHECK(ulp_load_binary(0, ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t)));

    gpio_num_t gpio_num = FLOW_SENSOR_PIN;
    int rtcio_num = rtc_io_number_get(gpio_num);
    assert(rtc_gpio_is_valid_gpio(gpio_num) && "GPIO used for pulse counting must be an RTC IO");

    /* These variables exist in auto-generated ulp_main.h */
    ulp_next_edge = 0;
    ulp_io_number = rtcio_num;
    ulp_edge_count_to_wake_up = 10;

    /* Setup for the RTC pin */
    rtc_gpio_init(gpio_num);
    rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(gpio_num);
    rtc_gpio_pullup_dis(gpio_num);
    rtc_gpio_hold_en(gpio_num);

    esp_deep_sleep_disable_rom_logging(); // suppress boot messages

    /* Set ULP wake up period to T = 20ms.
     * Minimum pulse width has to be T * (ulp_debounce_counter + 1) = 80ms.
     */
    ulp_set_wakeup_period(0, 20000);

    esp_sleep_enable_ulp_wakeup();
}