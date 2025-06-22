/*
 * 
 * Copyright (c) 2025 Jack Whitham
 * 
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * ventilation-system main
 *
 * See README.md: this is a sample project for the wifi_settings library at
 * https://github.com/jwhitham/pico-wifi-settings
 * 
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/cyw43_driver.h"
#include "pico/multicore.h"

#include "wifi_settings.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"

#include "lwip/udp.h"

#include "leds.h"
#include "temperature.h"
#include "settings.h"

#if PICO_CYW43_ARCH_POLL
#error "Expected interrupt settings"
#endif

#define MAX_REPORT_SIZE     100
#define ID_GET_STATUS_HANDLER (ID_FIRST_USER_HANDLER + 0)
#define ID_SET_RELAYS_HANDLER (ID_FIRST_USER_HANDLER + 1)


typedef enum {
    TEMP_COLD = 0,      // Close to freezing
    TEMP_MILD,          // Normal
    TEMP_HOT,           // Too hot
} temperature_t;

typedef enum {
    MODE_AUTO = 0,      // Automatic
    MODE_AUTO_DARK,     // Automatic, evening, increase power after dark
    MODE_MANUAL_OFF,    // manually set to off
    MODE_MANUAL_ON,     // manually set to on
    MODE_MANUAL_BOOST,  // manually set to boost
} manual_mode_t;

typedef enum {
    CONTROL_OFF = 0,    // off
    CONTROL_ON,         // on
    CONTROL_BOOST,      // boost
} control_mode_t;

typedef struct config_t {
    float                       cold_threshold;     // largest numerical value
    float                       not_cold_threshold;
    float                       not_hot_threshold;
    float                       hot_threshold;      // smallest numerical value
    int                         change_delay_s;
    ip_addr_t                   report_addr;
    int                         report_port;
    int                         report_interval_s;
    int                         manual_timeout_s;
    int                         control_port;
} config_t;

typedef struct control_status_t {
    config_t                    config;
    int                         heartbeat_counter;
    temperature_t               temperature_band;
    manual_mode_t               manual_mode;
    control_mode_t              next_control_mode;
    control_mode_t              current_control_mode;
    absolute_time_t             control_mode_update_time;
    absolute_time_t             report_update_time;
    absolute_time_t             manual_mode_end_time;
    float                       external_temperature_value;
    struct temperature_t*       temperature_handle;
    struct udp_pcb*             comms_pcb;
} control_status_t;

static bool is_manual_mode(manual_mode_t mode) {
    switch (mode) {
        case MODE_AUTO:
        case MODE_AUTO_DARK:
            return false;
        default:
            return true;
    }
}

static void make_report(control_status_t* cs, char* message, size_t size) {
    const char* control_text = "OFF";
    switch (cs->current_control_mode) {
        case CONTROL_ON:
            control_text = "ON";
            break;
        case CONTROL_BOOST:
            control_text = "BOOST";
            break;
        default:
            break;
    }

    const char* temp_text = "MILD";
    switch (cs->temperature_band) {
        case TEMP_COLD:
            temp_text = "COLD";
            break;
        case TEMP_HOT:
            temp_text = "HOT";
            break;
        default:
            break;
    }

    const uint64_t uptime = to_us_since_boot(get_absolute_time()) / 1000000ULL;
    const float internal_temperature_value = temperature_internal(cs->temperature_handle);

    snprintf(message, size,
        "ext %1.1f int %1.1f control %s auto %u temp %s up %u\n",
        (double) cs->external_temperature_value,
        (double) internal_temperature_value,
        control_text,
        is_manual_mode(cs->manual_mode) ? 0 : 1,
        temp_text,
        (unsigned) uptime);
}

static void make_report_and_send_by_udp(control_status_t* cs) {
    if ((!cs->config.report_port) || (!cs->comms_pcb)) {
        // reporting is disabled
        return;
    }
    char message[MAX_REPORT_SIZE];
    make_report(cs, message, sizeof(message));
    size_t size = strlen(message);

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
    if (!p) {
        return;
    }

    memcpy(p->payload, message, size);
    (void) udp_sendto(cs->comms_pcb, p, &cs->config.report_addr, cs->config.report_port);
    pbuf_free(p);
}


static void periodic_task(control_status_t* cs) {
    // Read temperature sensors
    temperature_update(cs->temperature_handle);
    cs->external_temperature_value = temperature_external(cs->temperature_handle);

    // Determine temperature range
    switch (cs->temperature_band) {
        case TEMP_COLD:
            if (cs->external_temperature_value > cs->config.not_cold_threshold) {
                cs->temperature_band = TEMP_MILD;
            }
            break;
        case TEMP_HOT:
            if (cs->external_temperature_value < cs->config.not_hot_threshold) {
                cs->temperature_band = TEMP_MILD;
            }
            break;
        case TEMP_MILD:
            if (cs->external_temperature_value > cs->config.hot_threshold) {
                cs->temperature_band = TEMP_HOT;
            }
            if (cs->external_temperature_value < cs->config.cold_threshold) {
                cs->temperature_band = TEMP_COLD;
            }
            break;
        default:
            cs->temperature_band = TEMP_MILD;
            break;
    }

    // Leave manual mode if the timeout is reached
    if (is_manual_mode(cs->manual_mode)
    && time_reached(cs->manual_mode_end_time)) {
        cs->manual_mode = MODE_AUTO;
    }

    // Decide which control mode to be in
    switch (cs->manual_mode) {
        case MODE_AUTO:
            if (cs->temperature_band == TEMP_MILD) {
                cs->next_control_mode = CONTROL_ON;
            } else {
                cs->next_control_mode = CONTROL_OFF;
            }
            break;
        case MODE_AUTO_DARK:
            if (cs->temperature_band == TEMP_MILD) {
                cs->next_control_mode = CONTROL_BOOST;
            } else {
                cs->next_control_mode = CONTROL_OFF;
            }
            break;
        case MODE_MANUAL_OFF:
            cs->next_control_mode = CONTROL_OFF;
            break;
        case MODE_MANUAL_ON:
            cs->next_control_mode = CONTROL_ON;
            break;
        case MODE_MANUAL_BOOST:
            cs->next_control_mode = CONTROL_BOOST;
            break;
        default:
            cs->next_control_mode = CONTROL_OFF;
            break;
    }

    // Change control mode if the update time is reached
    bool output_changed = false;
    if ((cs->next_control_mode != cs->current_control_mode)
    && time_reached(cs->control_mode_update_time)) {
        cs->current_control_mode = cs->next_control_mode;
        switch (cs->current_control_mode) {
            case CONTROL_ON:
                gpio_put(BOOST_RELAY_GPIO, 0);
                gpio_put(MAINS_RELAY_GPIO, 1);
                break;
            case CONTROL_BOOST:
                gpio_put(BOOST_RELAY_GPIO, 1);
                gpio_put(MAINS_RELAY_GPIO, 1);
                break;
            default:
                gpio_put(BOOST_RELAY_GPIO, 0);
                gpio_put(MAINS_RELAY_GPIO, 0);
                break;
        }
        cs->control_mode_update_time = make_timeout_time_ms(cs->config.change_delay_s * 1000);
        output_changed = true;
    }

    // Show the current state on the LEDs
    uint leds = POWER_LED_BIT; // power always on

    // Heartbeat LED
    leds |= (cs->heartbeat_counter == 0) ? HEARTBEAT_LED_BIT : 0;

    // Relay LEDs (red)
    switch (cs->current_control_mode) {
        case CONTROL_ON:
            leds |= MAINS_RELAY_LED_BIT;
            break;
        case CONTROL_BOOST:
            leds |= BOOST_RELAY_LED_BIT;
            leds |= MAINS_RELAY_LED_BIT;
            break;
        default:
            break;
    }

    // Temperature input LEDs (yellow)
    // Steady in auto mode, flashing in manual mode
    if (!is_manual_mode(cs->manual_mode)) {
        switch (cs->temperature_band) {
            case TEMP_COLD:
                leds |= COLD_LED_BIT;
                break;
            case TEMP_HOT:
                leds |= HOT_LED_BIT;
                break;
            default:
                break;
        }
    } else {
        switch (cs->heartbeat_counter) {
            case 1:
                leds |= HOT_LED_BIT;
                break;
            case 2:
                leds |= COLD_LED_BIT;
                break;
            default:
                break;
        }
    }

    // LED output
    leds_output_set(leds);

    // Heartbeat update: faster when WiFi is not working
    if ((cs->heartbeat_counter > 0) && !wifi_settings_is_connected()) {
        cs->heartbeat_counter = 0;
    } else if (cs->heartbeat_counter >= 9) {
        cs->heartbeat_counter = 0;
    } else {
        cs->heartbeat_counter++;
    }

    // UDP report: send periodic update
    if (time_reached(cs->report_update_time) || output_changed) {
        cs->report_update_time = delayed_by_ms(cs->report_update_time, cs->config.report_interval_s * 1000);
        make_report_and_send_by_udp(cs);
    }
}

static int32_t remote_handler_get_status(
        uint8_t msg_type,
        uint8_t* data_buffer,
        uint32_t input_data_size,
        int32_t input_parameter,
        uint32_t* output_data_size,
        void* arg) {

    control_status_t* cs = (control_status_t *) arg;
    uint32_t flags = save_and_disable_interrupts();
    switch (input_parameter) {
        case 0:
            // Text report
            make_report(cs, (char*) data_buffer, (size_t) *output_data_size);
            *output_data_size = strlen((char*) data_buffer);
            break;
        case 1:
            // Data structure dump
            if (*output_data_size > sizeof(control_status_t)) {
                *output_data_size = sizeof(control_status_t);
            }
            memcpy(data_buffer, cs, (size_t) *output_data_size);
            break;
        case 2:
            // Temperature report dump
            *output_data_size = temperature_copy(cs->temperature_handle, data_buffer, *output_data_size);
            break;
        default:
            *output_data_size = 0;
            break;
    }
    restore_interrupts(flags);
    return 0;
}

static bool manual_setting(control_status_t* cs, const char* command, size_t size) {
    uint32_t flags = save_and_disable_interrupts();
    bool ok = false;
    if (strncmp(command, "piv auto", size) == 0) {
        cs->manual_mode = MODE_AUTO;
        ok = true;
    } else if (strncmp(command, "piv dark", size) == 0) {
        cs->manual_mode = MODE_AUTO_DARK;
        ok = true;
    } else if (strncmp(command, "piv on", size) == 0) {
        cs->manual_mode = MODE_MANUAL_ON;
        ok = true;
    } else if (strncmp(command, "piv boost", size) == 0) {
        cs->manual_mode = MODE_MANUAL_BOOST;
        ok = true;
    } else if (strncmp(command, "piv off", size) == 0) {
        cs->manual_mode = MODE_MANUAL_OFF;
        ok = true;
    }
    if (ok) {
        cs->manual_mode_end_time = make_timeout_time_ms(cs->config.manual_timeout_s * 1000);
        make_report_and_send_by_udp(cs);
    }
    restore_interrupts(flags);
    return ok;
}

static int32_t remote_handler_set_relays(
        uint8_t msg_type,
        uint8_t* data_buffer,
        uint32_t input_data_size,
        int32_t input_parameter,
        uint32_t* output_data_size,
        void* arg) {
    control_status_t* cs = (control_status_t *) arg;
    *output_data_size = 0;
    if (!manual_setting(cs, (const char *) data_buffer, (size_t) input_data_size)) {
        return 1;
    }
    return 0;
}

static void comms_recv_callback(void *arg, struct udp_pcb *pcb,
        struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    control_status_t* cs = (control_status_t *) arg;
    (void) manual_setting(cs, (const char*) p->payload, (size_t) p->len);
    pbuf_free(p);
}

static int config_get_float(const char* key, float default_value) {
    char tmp[16];
    uint size = sizeof(tmp) - 1;
    if (!wifi_settings_get_value_for_key(key, tmp, &size)) {
        return default_value;
    }
    tmp[size] = '\0';
    char* end = NULL;
    float value = strtof(tmp, &end);
    if ((end[0] == '\0') && (end != tmp)) {
        // read at least one digit and reached the end of the string
        return value;
    }
    return default_value;
}

static int config_get_int(const char* key, int min_value, int default_value, int max_value) {
    char tmp[16];
    uint size = sizeof(tmp) - 1;
    if (!wifi_settings_get_value_for_key(key, tmp, &size)) {
        return default_value;
    }
    tmp[size] = '\0';
    char* end = NULL;
    long value = strtol(tmp, &end, 0);
    if ((end[0] == '\0') && (end != tmp)) {
        // read at least one digit and reached the end of the string
        if ((value >= (long) min_value) && (value <= (long) max_value)) {
            // value is within the allowed range
            return value;
        }
    }
    return default_value;
}

static void config_init(control_status_t* cs) {
    // min/max values for ADC readings
    cs->config.cold_threshold = config_get_float("cold_threshold", 0.0f);
    cs->config.not_cold_threshold = config_get_float("not_cold_threshold", 2.0f);
    cs->config.not_hot_threshold = config_get_float("not_hot_threshold", 28.0f);
    cs->config.hot_threshold = config_get_float("hot_threshold", 30.0f);

    // minimum time between change of activity
    cs->config.change_delay_s = config_get_int("change_delay_s", 1, 30, INT_MAX);

    // where to send messages
    char address[32];
    uint size = sizeof(address) - 1;
    if (wifi_settings_get_value_for_key("report_address", address, &size)) {
        address[size] = '\0';
        if (ipaddr_aton(address, &cs->config.report_addr)) {
            // Address is valid
            cs->config.report_port = config_get_int("report_port", 0, 0, UINT16_MAX);
            cs->config.report_interval_s = config_get_int("report_interval_s", 1, 30, INT_MAX);
        }
    }

    // where to listen for commands
    cs->config.control_port = config_get_int("control_port", 0, 0, UINT16_MAX);

    // timeout for manual settings
    cs->config.manual_timeout_s = config_get_int("manual_timeout_s", 1, 60 * 60 * 24, INT_MAX);
}

static void state_init(control_status_t* cs) {
    // initial setup
    memset(cs, 0, sizeof(control_status_t));
    cs->temperature_band = TEMP_MILD;
    cs->manual_mode = MODE_AUTO;
    cs->next_control_mode = CONTROL_OFF;
    cs->current_control_mode = CONTROL_OFF;

    // read config
    config_init(cs);

    // WiFi setup
    cs->comms_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (cs->comms_pcb && (udp_bind(cs->comms_pcb, NULL, cs->config.control_port) == 0)) {
        udp_recv(cs->comms_pcb, comms_recv_callback, cs);
    }

    // Temperature ADC setup
    cs->temperature_handle = temperature_init();
    while (!cs->temperature_handle) {
        printf("unable to allocate temperature_handle\n");
        sleep_ms(1000);
    }
    cs->external_temperature_value = temperature_external(cs->temperature_handle);

    // generate timeouts
    cs->control_mode_update_time = make_timeout_time_ms(cs->config.change_delay_s * 1000);
    cs->report_update_time = make_timeout_time_ms(cs->config.report_interval_s * 1000);
    cs->manual_mode_end_time = make_timeout_time_ms(cs->config.manual_timeout_s * 1000);
}

int main(void) {
    set_sys_clock_khz(48000, true);     // minimum frequency needed for USB
    cyw43_set_pio_clock_divisor(1, 0);  // needed so that cyw43 still works

    stdio_init_all();

    gpio_init(BOOST_RELAY_GPIO);
    gpio_set_dir(BOOST_RELAY_GPIO, GPIO_OUT);
    gpio_init(MAINS_RELAY_GPIO);
    gpio_set_dir(MAINS_RELAY_GPIO, GPIO_OUT);

    gpio_init(TEST_GPIO);
    gpio_set_dir(TEST_GPIO, GPIO_IN);
    gpio_pull_up(TEST_GPIO);

    leds_output_init();
    leds_output_set(~0);

    int rc = wifi_settings_init();
    while (rc != PICO_OK) {
        printf("wifi_settings_init returned %d\n", rc);
        sleep_ms(1000);
    }
    
    // Load configuration and initial state
    control_status_t* cs = malloc(sizeof(control_status_t));
    while (!cs) {
        printf("unable to allocate cs\n");
        sleep_ms(1000);
    }
    state_init(cs);
    wifi_settings_remote_set_handler(ID_GET_STATUS_HANDLER, remote_handler_get_status, cs);
    wifi_settings_remote_set_handler(ID_SET_RELAYS_HANDLER, remote_handler_set_relays, cs);
    wifi_settings_connect();

    // main loop
    absolute_time_t update_time = make_timeout_time_ms(0);
    while (true) {
        uint32_t flags = save_and_disable_interrupts();
        periodic_task(cs);
        restore_interrupts(flags);
        sleep_until(update_time);
        update_time = delayed_by_ms(update_time, 100);
    }
}
