#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "sys/time.h"
#include "esp_timer.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include "sensors.h"
#include "sample_batch.pb.h"
#include <math.h>
#include "driver/pulse_cnt.h"

static const char* TAG = "sensors";

static adc_oneshot_unit_handle_t adc1_handle;

static adc_cali_handle_t pressure_sensor_cali_handle;
static pcnt_unit_handle_t pcnt_unit = NULL;

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);

static inline float round3(float v) {
    return roundf(v * 1000.0f) / 1000.0f;
}

static inline float normalize_pressure(int millivolts) {
    float voltage = millivolts / 1000.0f;
    float pressure = voltage / PRESSURE_SENSOR_VOLTAGE_MAX;
    return round3(pressure);
}

static inline float normalize_flow(uint32_t pulse_count) {
    float flow_rate = (float)pulse_count / FLOW_SENSOR_PULSES_PER_LITER;
    float flow_rate_norm = flow_rate / MAX_FLOW_LPM;
    return round3(flow_rate_norm);
}

static inline float pressure_sensor_read() {
    int pressure;
    adc_oneshot_get_calibrated_result(adc1_handle, pressure_sensor_cali_handle, PRESSURE_SENSOR_CHANNEL, &pressure);
    return normalize_pressure(pressure);
}

static inline float flow_sensor_read() {
    int pulse_count;
    pcnt_unit_get_count(pcnt_unit, &pulse_count);
    pcnt_unit_clear_count(pcnt_unit);
    return normalize_flow(pulse_count);
}

void flow_sensor_init(void) {
    ESP_LOGI(TAG, "install pcnt unit");
    pcnt_unit_config_t unit_config = {
        .high_limit = 500,
        .low_limit = -500,
    };
    
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    ESP_LOGI(TAG, "set glitch filter");
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    ESP_LOGI(TAG, "install pcnt channels");
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = GPIO_NUM_25,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE));

    ESP_LOGI(TAG, "enable pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_LOGI(TAG, "clear pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_LOGI(TAG, "start pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
}

void pressure_sensor_init(void) {
    adc_oneshot_unit_init_cfg_t init_config_adc1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config_adc1, &adc1_handle));

    adc_oneshot_chan_cfg_t config_adc1 = {
        .atten = PRESSURE_SENSOR_ADC_ATTENUATION,
        .bitwidth = PRESSURE_SENSOR_ADC_WIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, PRESSURE_SENSOR_CHANNEL, &config_adc1));
    bool do_pressure_sensor_calibration = adc_calibration_init(ADC_UNIT_1, PRESSURE_SENSOR_CHANNEL, PRESSURE_SENSOR_ADC_ATTENUATION, &pressure_sensor_cali_handle);
    ESP_LOGI(TAG, "Pressure calibrated: %d", do_pressure_sensor_calibration);
}

void app_sensors_init(void) {
    pressure_sensor_init();
    flow_sensor_init();
}

void app_sensors_read(Sample* sample) {

    struct timeval tv;
    gettimeofday(&tv, NULL);

    sample->timestamp = ((uint64_t)tv.tv_sec * 1000);
    sample->pressure = pressure_sensor_read();
    sample->flow = flow_sensor_read();

    ESP_LOGI(TAG, "Timestamp: %llu, Pressure: %.4f, Flow: %.4f", sample->timestamp, sample->pressure, sample->flow);
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

// Available in ESP32-C3
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = PRESSURE_SENSOR_ADC_WIDTH,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
     if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = PRESSURE_SENSOR_ADC_WIDTH,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}