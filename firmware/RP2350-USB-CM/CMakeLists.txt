cmake_minimum_required(VERSION 3.13)

# Board: Waveshare RP2350-USB-CM. The Pico SDK has no definition for it, so
# one is kept in boards/. The roles of the two ports are the REVERSE of the
# RP2350-USB-A and RP2350-USB-C, because here the type C *plug* is the PIO
# USB port:
#   PIO USB port (type C plug, GPIO 13/12) --> device: the gamepad, to the computer
#   native USB port (type C receptacle)    --> host: keyboard / mouse
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD waveshare_rp2350_usb_cm CACHE STRING "Board type")

# Pico-PIO-USB is needed for the PIO USB device port. Either pass
# -DPICO_PIO_USB_PATH=<path> or clone it next to this file
if(NOT DEFINED PICO_PIO_USB_PATH)
    set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/Pico-PIO-USB)
endif()

# Pico SDK location is taken from PICO_SDK_PATH
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(v32io_usb_cm C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

if(NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "Pico-PIO-USB not found: set PICO_PIO_USB_PATH")
endif()

add_executable(v32io_usb_cm
    src/main.c
    src/kbd.c
    src/keymap.c
    src/compose.c
    src/mouse.c
    src/led.c
    src/usb_descriptors.c
)

# status light: WS2812 driven with PIO (PIO 2; the PIO USB device uses PIO 0)
pico_generate_pio_header(v32io_usb_cm ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_include_directories(v32io_usb_cm PRIVATE src)

target_link_libraries(v32io_usb_cm PRIVATE
    pico_stdlib
    hardware_pio
    tinyusb_device
    tinyusb_host
    tinyusb_pico_pio_usb
)

# TinyUSB's PIO USB device driver always uses D+ = GPIO 12, D- = GPIO 13.
# This board has them the other way around, so main.c wraps the call that
# starts it (pio_usb_device_init) to give it the right pins.
target_link_options(v32io_usb_cm PRIVATE -Wl,--wrap=pio_usb_device_init)

# no stdio: both USB ports are in use by the adapter itself
pico_enable_stdio_usb(v32io_usb_cm 0)
pico_enable_stdio_uart(v32io_usb_cm 0)

# generates v32io_usb_cm.uf2
pico_add_extra_outputs(v32io_usb_cm)
