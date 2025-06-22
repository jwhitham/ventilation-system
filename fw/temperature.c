/*
 * 
 * Copyright (c) 2025 Jack Whitham
 * 
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * ventilation-system temperature ADC driver
 *
 * This component reads the temperature using the ADC, applies filtering
 * to reduce noise, and returns values in degrees Celsius.
 * 
 */

#include "settings.h"
#include "temperature.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/adc.h"

#define ADC_FULL_SCALE      (1 << 12)
#define HISTORY_SIZE        100     // Temperatures averaged over 100 samples
#define ADC_REF_VOLTAGE     3.3f
#define MAX_REPORT_SIZE     2000

typedef struct sensor_history_t {
    int         index;
    int         total;
    int16_t     data[HISTORY_SIZE];
} sensor_history_t;

typedef struct temperature_t {
    sensor_history_t    internal_sensor_history;
    sensor_history_t    external_sensor_history;
    int16_t             report_data[MAX_REPORT_SIZE];
    uint32_t            report_index;
} temperature_t;


static void update_history(sensor_history_t* sh, int16_t new_value) {
    int16_t old_value = sh->data[sh->index];
    sh->data[sh->index] = new_value;
    sh->total += (int) new_value - (int) old_value;
    sh->index++;
    if (sh->index >= HISTORY_SIZE) {
        sh->index = 0;
    }
}

void temperature_update(struct temperature_t* t) {
    adc_select_input(4);
    update_history(&t->internal_sensor_history, adc_read());
    adc_select_input(2);
    const int16_t a = adc_read();
    update_history(&t->external_sensor_history, a);
    if (t->report_index >= MAX_REPORT_SIZE) {
        t->report_index = 0;
    }
    t->report_data[t->report_index] = a;
    t->report_index++;
}

struct temperature_t* temperature_init() {
    temperature_t* t = calloc(1, sizeof(temperature_t));
    if (!t) {
        return NULL;
    }

    adc_init();
    adc_set_temp_sensor_enabled(true);

    gpio_set_dir(ADC_PIN, GPIO_IN);
    gpio_set_function(ADC_PIN, GPIO_FUNC_SIO);
    gpio_disable_pulls(ADC_PIN);
    gpio_set_input_enabled(ADC_PIN, false);

    // fill the history with the current temperature
    for (int i = 0; i < HISTORY_SIZE; i++) {
        temperature_update(t);
    }
    return t;
}

float temperature_internal(const struct temperature_t* t) {
    // calculate voltage from sample value
    const float voltage = (ADC_REF_VOLTAGE * (float) t->internal_sensor_history.total) / (float) (ADC_FULL_SCALE * HISTORY_SIZE);
    // Apply the equation from the RP-2350 data sheet (section 12.4.6, "Temperature Sensor")
    // Return value in Celsius
    return 27.0f - ((voltage - 0.706f) / 0.001721f);
}

float temperature_external(const struct temperature_t* t) {
    // sample value in 0.0 .. 1.0 range
    const float fraction = (float) t->external_sensor_history.total / (float) (ADC_FULL_SCALE * HISTORY_SIZE);
    const float CtoK = 273.15f;  // return value will be in Celsius
    // Range check
    if (fraction < 0.015f) {
        // very hot
        return 1000.0f;
    } else if (fraction > 0.985f) {
        // very cold
        return -CtoK;
    }
    // calculate resistance of thermistor (there is a voltage divider with a fixed 15k resistance)
    const float r2 = 15000.0f;
    const float r1 = r2 / ((1.0f / fraction) - 1.0f);
    // Apply a simplified version of the Steinhart-Hart equation with C = 0
    // The thermistor type is not known and I have no official data on it, but I got the approximate B value experimentally
    const float A = 1.0f / (CtoK + 25.0f);
    const float B = 0.000305267f;
    const float Rref = 15.0e3f;
    const float ratio = logf(r1 / Rref);
    return (1.0f / (A + (B * ratio))) - CtoK;
}

uint32_t temperature_copy(struct temperature_t* t, void* payload, uint32_t max_size) {
    uint32_t size = t->report_index * 2;
    if (size > max_size) {
        size = max_size;
    }
    memcpy(payload, t->report_data, size);
    t->report_index = 0;
    return size;
}
