#include "soil_moisture.h"

#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

/*
 * Wiring (docs/breadboard-plan.png):
 *   sensor VCC  -> 3V3 (lower + rail)
 *   sensor GND  -> GND (lower - rail)
 *   sensor AOUT -> IO0  == ADC1 channel 0 on the ESP32-C3
 */
#define SOIL_ADC_UNIT     ADC_UNIT_1
#define SOIL_ADC_CHANNEL  ADC_CHANNEL_0
/* 12 dB attenuation: measurable range up to ~2.5 V on the ESP32-C3 */
#define SOIL_ADC_ATTEN    ADC_ATTEN_DB_12
#define SOIL_ADC_BITWIDTH ADC_BITWIDTH_DEFAULT

static const char* TAG = "soil";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;

static bool cali_init(void)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = SOIL_ADC_UNIT,
        .chan = SOIL_ADC_CHANNEL,
        .atten = SOIL_ADC_ATTEN,
        .bitwidth = SOIL_ADC_BITWIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&cfg, &s_cali) == ESP_OK) {
        return true;
    }
#endif
    ESP_LOGW(TAG, "ADC calibration not available, voltages are estimated");
    return false;
}

esp_err_t soil_moisture_init(void)
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = SOIL_ADC_UNIT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "ADC unit init failed");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = SOIL_ADC_ATTEN,
        .bitwidth = SOIL_ADC_BITWIDTH,
    };
    esp_err_t err = adc_oneshot_config_channel(s_adc, SOIL_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return err;
    }

    s_cali_ok = cali_init();
    ESP_LOGI(TAG, "Soil moisture sensor ready (dry=%d mV, wet=%d mV)",
             CONFIG_SOIL_MOISTURE_DRY_MV, CONFIG_SOIL_MOISTURE_WET_MV);
    return ESP_OK;
}

static int voltage_to_percent(int mv)
{
    const int dry = CONFIG_SOIL_MOISTURE_DRY_MV;
    const int wet = CONFIG_SOIL_MOISTURE_WET_MV;
    if (dry == wet) {
        return 0;
    }
    /* Capacitive sensor: higher voltage = drier soil */
    int pct = (dry - mv) * 100 / (dry - wet);
    if (pct < 0) {
        pct = 0;
    } else if (pct > 100) {
        pct = 100;
    }
    return pct;
}

esp_err_t soil_moisture_read(soil_moisture_reading_t* out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "out is NULL");
    ESP_RETURN_ON_FALSE(s_adc, ESP_ERR_INVALID_STATE, TAG, "not initialised");

    const int samples = CONFIG_SOIL_MOISTURE_SAMPLES;
    int raw_sum = 0;
    int mv_sum = 0;

    for (int i = 0; i < samples; i++) {
        int raw;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, SOIL_ADC_CHANNEL, &raw), TAG, "ADC read failed");
        raw_sum += raw;

        int mv;
        if (s_cali_ok) {
            ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw, &mv), TAG, "calibration failed");
        } else {
            /* Rough linear estimate for 12 dB attenuation (~2500 mV full scale) */
            mv = raw * 2500 / 4095;
        }
        mv_sum += mv;
    }

    out->raw = raw_sum / samples;
    out->voltage_mv = mv_sum / samples;
    out->percent = voltage_to_percent(out->voltage_mv);
    return ESP_OK;
}

void soil_moisture_deinit(void)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (s_cali_ok) {
        adc_cali_delete_scheme_curve_fitting(s_cali);
        s_cali_ok = false;
    }
#endif
    if (s_adc) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
    }
}
