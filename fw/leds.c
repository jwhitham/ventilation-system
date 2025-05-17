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

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/flash.h"

#include "leds.h"
#include "settings.h"

#include "leds.pio.h"

#define LEDS_PIO (pio0)
#define LEDS_SM (0)
#define LED_SM_FREQ     1000000

#define NUM_LEDS        6

void leds_output_init(void) {
    const uint first_pin = WHITE_WIRE_GPIO;
    const uint num_pins = NUM_LEDS / 2;
    uint offset = 0;

    // Install PIO program
    offset = pio_add_program(LEDS_PIO, &leds_program);

    // Configuration updated
    pio_sm_config c = leds_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&c, clock_get_hz(clk_sys) / LED_SM_FREQ, 0);
    sm_config_set_out_pins(&c, first_pin, num_pins);
    sm_config_set_set_pins(&c, first_pin, num_pins);
    // Set this pin's GPIO function (connect PIO to the pad)
    for (uint i = 0; i < num_pins; i++) {
        gpio_init(first_pin + i);
        gpio_set_drive_strength(first_pin + i, GPIO_DRIVE_STRENGTH_12MA);
        pio_gpio_init(LEDS_PIO, first_pin + i);
    }
    // Load our configuration, and jump to the start of the program
    pio_sm_init(LEDS_PIO, LEDS_SM, offset, &c);
    // Set the state machine running
    pio_sm_set_enabled(LEDS_PIO, LEDS_SM, true);
}

// This table shows which of the bits sent to PIO should be "on", in
// order to activate the LED. This is the "pins: xxx" value seen in
// leds.pio for each LED.
static const uint8_t on_bit_table[NUM_LEDS] = {1, 2, 1, 4, 2, 4};

void leds_output_set(const uint8_t state) {
    uint32_t control_value = 0;

    for (uint i = 0; i < NUM_LEDS; i++) {
        if ((state >> i) & 1) {
            control_value |= on_bit_table[i] << (i * 3);
        }
    }
    pio_sm_put_blocking(LEDS_PIO, LEDS_SM, control_value);
}
