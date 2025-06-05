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
#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdint.h>

struct temperature_t;
struct temperature_t* temperature_init();
float temperature_internal(const struct temperature_t* t);
float temperature_external(const struct temperature_t* t);
void temperature_update(struct temperature_t* t);
uint32_t temperature_copy(struct temperature_t* t, void* payload, uint32_t max_size);

#endif
