#include "sntp.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"

#include "esp_sntp.h"

#include "common.h"

static const char* TAG = "sntp";

void sntp_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "System time synchronized.");
    xEventGroupSetBits(app_event_group, TIME_SYNCED_BIT);
}

void app_sntp_init(void) {
    ESP_LOGI(TAG, "Initializing SNTP");

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com", "time.cloudflare.com"));
    config.start = false;                       // start SNTP service explicitly (after connecting)
    config.server_from_dhcp = true;             // accept NTP offers from DHCP server, if any (need to enable *before* connecting)
    config.renew_servers_after_new_IP = true;   // let esp-netif update configured SNTP server(s) after receiving DHCP lease
    config.index_of_first_server = 1;           // updates from server num 1, leaving server 0 (from DHCP) intact
    // configure the event on which we renew servers
    config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
    config.sync_cb = sntp_sync_notification_cb; // only if we need the notification function
    esp_netif_sntp_init(&config);
}

void app_sntp_start(void) {
    esp_netif_sntp_start();
}