set(components "." "sntp" "prov" "mqtt" "sensors" "common" "proto")
set(dependencies bt esp_wifi nvs_flash wifi_provisioning mqtt esp_driver_gpio esp_adc nanopb esp_pm ulp soc esp_driver_pcnt)

idf_component_register(
    REQUIRES ${dependencies}
    INCLUDE_DIRS ${components}
    SRC_DIRS ${components}
)

set(ulp_app_name "ulp_main")
set(ulp_s_sources "./ulp/pulse_count.S" "./ulp/wake_up.S")
set(ulp_exp_dep_srcs "./mqtt/mqtt.c" "main.c")
ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")