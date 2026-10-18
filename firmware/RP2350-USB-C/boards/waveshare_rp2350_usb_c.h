/*
 * Board definition for the Waveshare RP2350-USB-C (the Pico SDK has none).
 * Pins taken from the manufacturer's schematic and PIO-USB example
 * (github.com/waveshareteam/RP2350-USB-C). Modeled on the SDK's own
 * waveshare_rp2350_usb_a.h, which this board is nearly identical to.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _BOARDS_WAVESHARE_RP2350_USB_C_H
#define _BOARDS_WAVESHARE_RP2350_USB_C_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define WAVESHARE_RP2350_USB_C

#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64
#endif

#define PICO_RP2350A 1

// WS2812B RGB LED
#ifndef PICO_DEFAULT_WS2812_PIN
#define PICO_DEFAULT_WS2812_PIN 16
#endif

// PIO USB port (the second type C connector, a receptacle on this variant):
// D+ is GPIO 12 and D- is GPIO 13 (DM = DP + 1)
#ifndef WAVESHARE_RP2350_USB_C_PIO_USB_DP_PIN
#define WAVESHARE_RP2350_USB_C_PIO_USB_DP_PIN 12
#endif
#ifndef WAVESHARE_RP2350_USB_C_PIO_USB_DM_PIN
#define WAVESHARE_RP2350_USB_C_PIO_USB_DM_PIN 13
#endif
#define PICO_DEFAULT_PIO_USB_DP_PIN WAVESHARE_RP2350_USB_C_PIO_USB_DP_PIN

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 3
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
