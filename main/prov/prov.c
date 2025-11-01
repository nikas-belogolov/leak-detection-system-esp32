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

#include "common.h"
#include "mqtt.h"
#include <protocomm_security.h>
#include <protocomm_security1.h>

#include "esp_chip_info.h"

static const char* TAG = "prov";

#define PROV_DEVICE_ID_ENDPOINT_NAME "device-id"
/* Signal Wi-Fi events on this event-group */
// const int WIFI_CONNECTED_EVENT = BIT0;
#define MAX_RETRIES 3
static int connection_retries = 0;


static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "Irrigo ESP32-";

    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

void app_wifi_init() {
    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

/* Event handler for catching system events */
void prov_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                wifi_prov_mgr_reset_sm_state_on_failure();
                app_mqtt_stop();
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END: {
                wifi_prov_mgr_deinit();
                break;
            }
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Wi-Fi disconnected, retrying connection...");
                xEventGroupClearBits(app_event_group, WIFI_CONNECTED_BIT);
                ESP_LOGI(TAG, "Reconnecting to WiFi...");
                esp_wifi_connect();
                vTaskDelay(30000 / portTICK_PERIOD_MS);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(app_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t device_id_endpoint_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                                uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    const char* device_id = app_get_device_id();
    size_t len = strlen(device_id);

    uint8_t *resp = malloc(len);
    if (!resp) {
        ESP_LOGE(TAG, "malloc failed");
        return ESP_ERR_NO_MEM;
    }
    memcpy(resp, device_id, len);

    *outbuf = resp;
    *outlen = len;

    return ESP_OK;
}

void app_prov_init() {

    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        // .wifi_prov_conn_cfg = {
        //    .wifi_conn_attempts =  3,
        // },
        .scheme = wifi_prov_scheme_ble,
        // .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
        // .app_event_handler = wifi_prov_event_handler,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
    wifi_prov_mgr_disable_auto_stop(5000);

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &prov_event_handler, NULL));
}

void print_uuid(const uint8_t uuid[16]) {
    printf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
        uuid[15], uuid[14], uuid[13], uuid[12],  // First 8 characters (32 bits)
        uuid[11], uuid[10],                      // Next 4 characters (16 bits)
        uuid[9], uuid[8],                        // Next 4 characters (16 bits)
        uuid[7], uuid[6],                        // Next 4 characters (16 bits)
        uuid[5], uuid[4], uuid[3], uuid[2], uuid[1], uuid[0]  // Last 12 characters (48 bits)
    );
}

void app_prov_start() {
    ESP_LOGI(TAG, "Starting provisioning");

    char service_name[21];
    get_device_service_name(service_name, sizeof(service_name));
    
    
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *pop = NULL;
    wifi_prov_security1_params_t *sec_params = pop;


    uint8_t custom_service_uuid[] = {
        /* LSB <---------------------------------------
        * ---------------------------------------> MSB */
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };

    print_uuid((const uint8_t*)custom_service_uuid);

    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

    wifi_prov_mgr_endpoint_create(PROV_DEVICE_ID_ENDPOINT_NAME);

    /* Do not stop and de-init provisioning even after success,
    * so that we can restart it later. */

    /* Start provisioning service */
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *) sec_params, service_name, NULL));

    wifi_prov_mgr_endpoint_register(PROV_DEVICE_ID_ENDPOINT_NAME, device_id_endpoint_handler, NULL);
}

void app_prov_stop() {
    wifi_prov_mgr_stop_provisioning();
    ESP_LOGI(TAG, "Provisioning stopped");
}

