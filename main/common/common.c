#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "common.h"
#include "mqtt.h"

#include "esp_mac.h"

static const char* TAG = "COMMON";

static char device_id[32];

void app_device_id_init(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

const char* app_get_device_id(void) {
    return device_id;
}