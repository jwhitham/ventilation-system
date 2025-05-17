/*
 * 
 * Copyright (c) 2025 Jack Whitham
 * 
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 * ventilation-system mbedtls_config.h
 *
 * This file configures the mbedtls library (crypto functions
 * used by wifi_settings).
 * 
 */
#ifndef _MBEDTLS_CONFIG_H
#define _MBEDTLS_CONFIG_H

#if LIB_PICO_SHA256
// Enable hardware acceleration
#define MBEDTLS_SHA256_ALT
#else
#define MBEDTLS_SHA256_C
#endif
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_MODE_CBC

#endif
