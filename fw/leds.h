/*
 * 
 * Copyright (c) 2025 Jack Whitham
 * 
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * LED driver for ventilation-system project
 * 
 * Six LEDs are connected to three GPIO pins in a matrix arrangement,
 * a PIO program drives each LED in turn. This arrangement reduces the
 * amount of physical wiring required.
 */

#ifndef LEDS_H
#define LEDS_H

#define MIDDLE_RIGHT_LED    0       // yellow
#define MIDDLE_LEFT_LED     1       // red
#define MIDDLE_UPPER_LED    2       // green
#define TOP_LED             3       // green
#define LOWER_LEFT_LED      4       // red
#define LOWER_RIGHT_LED     5       // yellow

#define MIDDLE_RIGHT_LED_BIT    (1 << MIDDLE_RIGHT_LED)
#define MIDDLE_LEFT_LED_BIT     (1 << MIDDLE_LEFT_LED)
#define MIDDLE_UPPER_LED_BIT    (1 << MIDDLE_UPPER_LED)
#define TOP_LED_BIT             (1 << TOP_LED)
#define LOWER_LEFT_LED_BIT      (1 << LOWER_LEFT_LED)
#define LOWER_RIGHT_LED_BIT     (1 << LOWER_RIGHT_LED)

#define POWER_LED_BIT           MIDDLE_UPPER_LED_BIT
#define HEARTBEAT_LED_BIT       TOP_LED_BIT
#define MAINS_RELAY_LED_BIT     MIDDLE_LEFT_LED_BIT
#define BOOST_RELAY_LED_BIT     LOWER_LEFT_LED_BIT
#define COLD_LED_BIT            LOWER_RIGHT_LED_BIT
#define HOT_LED_BIT             MIDDLE_RIGHT_LED_BIT


void leds_output_init(void);
void leds_output_set(const uint8_t state);

#endif
