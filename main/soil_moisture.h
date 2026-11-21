#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One measurement from the capacitive soil moisture sensor. */
typedef struct {
    int raw;          /**< Averaged raw ADC value (0..4095) */
    int voltage_mv;   /**< Averaged voltage in mV (calibrated if available, otherwise estimated) */
    int percent;      /**< Moisture in percent: 0 = dry (air), 100 = wet (water) */
} soil_moisture_reading_t;

/**
 * Set up the ADC for the sensor (AOUT on GPIO0 / ADC1_CH0, see docs/breadboard-plan.png).
 * Must be called once before soil_moisture_read().
 */
esp_err_t soil_moisture_init(void);

/** Take several samples, average them and convert to voltage and percent. */
esp_err_t soil_moisture_read(soil_moisture_reading_t* out);

/** Release the ADC resources. */
void soil_moisture_deinit(void);

#ifdef __cplusplus
}
#endif
