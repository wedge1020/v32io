#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

// RP2350-USB-CM: the roles are the reverse of the RP2350-USB-A / -USB-C
// port 0: native USB (type C receptacle) --> host, where the keyboard is plugged
// port 1: PIO USB (type C plug)          --> device, seen by the PC as a gamepad
#define BOARD_TUH_RHPORT            0
#define BOARD_TUD_RHPORT            1

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUD_RPI_PIO_USB         1

#define CFG_TUH_ENABLED             1
#define CFG_TUH_MAX_SPEED           OPT_MODE_FULL_SPEED
// (the host is the chip's own USB controller, not PIO USB)

// ---------------- device configuration
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_HID                 1
#define CFG_TUD_HID_EP_BUFSIZE      16

// ---------------- host configuration
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// hub support allows keyboards with built-in hubs
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          4
#define CFG_TUH_HID                 8
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64

#endif
