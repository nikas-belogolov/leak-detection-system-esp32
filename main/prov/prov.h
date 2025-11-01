#ifndef PROV_H_
#define PROV_H_

void prov_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

void app_wifi_init();

void app_prov_init();
void app_prov_start();
void app_prov_stop();

#endif