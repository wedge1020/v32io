//////////////////////////////////////////////////////////////////////////////
//
// v32io-pico: hardware v32io adapter for Waveshare RP2350-USB-A
// ----------------------------------------------------------------------
// A USB keyboard or mouse is plugged into the board's type A port (USB
// host, done with PIO on GPIO 12/13). The type C port is plugged into the
// computer, which sees an 11-button USB gamepad:
//
//   - with a keyboard: "v32io:kbd",   using the v32kbd protocol (v32kbd.h)
//   - with a mouse:    "v32io:mouse", using the v32mouse protocol (v32mouse.h)
//
// Vircon32 programs read them with keyboard.h and mouse.h, both built on
// the v32io.h core.
//
//////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "host/hcd.h"
#include "pio_usb.h"
#include "v32io.h"
#include "v32kbd.h"
#include "v32mouse.h"
#include "led.h"

//////////////////////////////////////////////////////////////////////////////
//
// Which device decides the mode when a keyboard and a mouse are plugged in
// at the same time (through a hub, or a wireless receiver for both):
//
//   MODE_FIRST:    the one that was plugged in first (for a single device
//                  with both, such as a wireless receiver, its first one)
//   MODE_KEYBOARD: always the keyboard
//   MODE_MOUSE:    always the mouse
//
#define MODE_FIRST      0
#define MODE_KEYBOARD   1
#define MODE_MOUSE      2

#define MODE_PRIORITY   MODE_FIRST

//////////////////////////////////////////////////////////////////////////////
//
// Changing mode makes the gamepad unplug from the PC and plug back in as a
// different device. It stays unplugged for this long, so the PC notices
//
#define REPLUG_MS       300

//////////////////////////////////////////////////////////////////////////////
//
// The gamepad only exists for the PC while there is an input device: with
// none, the type C port stays electrically disconnected (it still powers
// the board), and the PC sees the gamepad being unplugged.
//
static v32io_mode_t mode            = V32IO_MODE_NONE;  // what the PC sees
static bool         gamepad_enabled = false;
static uint32_t     unplugged_at    = 0;

// A device was plugged into the type A port and is being set up. This
// is only used for the status light. The flag is set from the USB host
// stack (maybe in an interrupt) and taken by the main loop.
#define CONNECT_TIMEOUT_MS  5000
static volatile bool attach_event = false;
static volatile bool remove_event = false;
static bool     connecting       = false;
static uint32_t connecting_since = 0;

//////////////////////////////////////////////////////////////////////////////
//
// Gamepad state
//
static uint16_t buttons      = 0;       // state we want the PC to see
static uint16_t buttons_sent = 0;       // state last sent to the PC
static bool     report_sent  = false;   // nothing was sent yet

uint32_t v32io_millis( void )
{
    return to_ms_since_boot( get_absolute_time() );
}

v32io_mode_t v32io_mode( void )
{
    return mode;
}

bool v32io_delivered( uint16_t state )
{
    return report_sent && buttons_sent == state;
}

//////////////////////////////////////////////////////////////////////////////
//
// Input devices plugged into the type A port. Each HID interface is one
// entry, so a wireless receiver for keyboard and mouse makes 2 of them
//
typedef struct
{
    bool                  used;
    uint8_t               dev_addr, instance;
    v32io_mode_t          type;         // keyboard or mouse
    uint32_t              order;        // to know which came first
    hid_keyboard_report_t last;         // keyboards: their last report
    uint8_t               mouse_buttons;    // mice: buttons held
}
input_t;

#define MAX_INPUTS  CFG_TUH_HID
static input_t  inputs[ MAX_INPUTS ];
static uint32_t inputs_plugged = 0;     // to number them in order

static input_t* find_input( uint8_t dev_addr, uint8_t instance )
{
    for( int i = 0; i < MAX_INPUTS; i++ )
      if( inputs[i].used && inputs[i].dev_addr == dev_addr && inputs[i].instance == instance )
        return &inputs[i];

    return NULL;
}

static input_t* add_input( uint8_t dev_addr, uint8_t instance, v32io_mode_t type )
{
    for( int i = 0; i < MAX_INPUTS; i++ )
      if( !inputs[i].used )
      {
          memset( &inputs[i], 0, sizeof(input_t) );
          inputs[i].used     = true;
          inputs[i].dev_addr = dev_addr;
          inputs[i].instance = instance;
          inputs[i].type     = type;
          inputs[i].order    = inputs_plugged++;
          return &inputs[i];
      }

    return NULL;
}

// the mode that the plugged devices ask for
static v32io_mode_t wanted_mode( void )
{
    input_t* first     = NULL;
    bool     has_kbd   = false;
    bool     has_mouse = false;

    for( int i = 0; i < MAX_INPUTS; i++ )
    {
        if( !inputs[i].used ) continue;

        if( inputs[i].type == V32IO_MODE_KBD   ) has_kbd   = true;
        if( inputs[i].type == V32IO_MODE_MOUSE ) has_mouse = true;

        if( !first || inputs[i].order < first->order )
          first = &inputs[i];
    }

    if( !first ) return V32IO_MODE_NONE;

  #if MODE_PRIORITY == MODE_KEYBOARD
    if( has_kbd ) return V32IO_MODE_KBD;
  #elif MODE_PRIORITY == MODE_MOUSE
    if( has_mouse ) return V32IO_MODE_MOUSE;
  #endif

    (void) has_kbd; (void) has_mouse;
    return first->type;
}

// buttons held, among all mice
static uint8_t all_mouse_buttons( void )
{
    uint8_t result = 0;

    for( int i = 0; i < MAX_INPUTS; i++ )
      if( inputs[i].used && inputs[i].type == V32IO_MODE_MOUSE )
        result |= inputs[i].mouse_buttons;

    return result;
}

//////////////////////////////////////////////////////////////////////////////
//
// Connects the gamepad to the PC while there is an input device, as the
// device that the current mode asks for
//
static void gamepad_task( void )
{
    uint32_t     now    = v32io_millis();
    v32io_mode_t wanted = wanted_mode();

    // a different mode (or none): unplug from the PC
    if( gamepad_enabled && wanted != mode )
    {
        tud_disconnect();
        gamepad_enabled = false;
        unplugged_at    = now;
    }

    if( gamepad_enabled )
      return;

    // while unplugged the mode can change, since the
    // PC will ask for our descriptors again
    if( wanted != mode )
    {
        mode = wanted;
        kbd_reset();
        mouse_reset();
    }

    if( mode == V32IO_MODE_NONE || now - unplugged_at < REPLUG_MS )
      return;

    tud_connect();
    gamepad_enabled = true;

    // mouse buttons may be held already
    if( mode == V32IO_MODE_MOUSE )
      mouse_input( all_mouse_buttons(), 0, 0 );

    // either way, the PC knows nothing of our state now
    report_sent  = false;
    buttons_sent = 0;
}

//////////////////////////////////////////////////////////////////////////////
//
// The buttons we want the PC to see, from the device mode
//
static void mode_task( void )
{
    switch( mode )
    {
        case V32IO_MODE_KBD:   buttons = kbd_task();   break;
        case V32IO_MODE_MOUSE: buttons = mouse_task(); break;
        default:               buttons = 0;            break;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Status light
//
static void status_task( void )
{
    uint32_t now = v32io_millis();

    if( attach_event )
    {
        attach_event     = false;
        connecting       = true;
        connecting_since = now;
    }

    if( remove_event )
    {
        remove_event = false;
        connecting   = false;
    }

    // give up on devices that never become a keyboard or mouse
    if( connecting && now - connecting_since > CONNECT_TIMEOUT_MS )
      connecting = false;

    led_link_t link  = LED_LINK_NONE;
    bool       setup = false;

    if( mode == V32IO_MODE_KBD )
    {
        link  = LED_LINK_KEYBOARD;
        setup = kbd_setup_mode();
    }

    else if( mode == V32IO_MODE_MOUSE )
    {
        link  = LED_LINK_MOUSE;
        setup = mouse_setup_mode();
    }

    else if( connecting )
      link = LED_LINK_CONNECTING;

    led_task( link, setup );
}

//////////////////////////////////////////////////////////////////////////////
//
// Sends the gamepad state to the PC when it has changed
//
static void report_task( void )
{
    if( !gamepad_enabled || !tud_mounted() )
    {
        // the PC knows nothing of our state: begin again when it does
        report_sent = false;
        return;
    }

    if( report_sent && buttons == buttons_sent )
      return;

    if( !tud_hid_ready() )
      return;

    v32io_report_t report = { .buttons = buttons, .x = 0, .y = 0 };

    if( tud_hid_report( 0, &report, sizeof(report) ) )
    {
        buttons_sent = buttons;
        report_sent  = true;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Main
//
int main( void )
{
    // PIO USB needs a system clock that is a multiple of 12 MHz
    set_sys_clock_khz( 120000, true );

    // native USB port: device (gamepad)
    tusb_rhport_init_t device_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
    tusb_init( BOARD_TUD_RHPORT, &device_init );

    // but stay disconnected from the PC until there is an input device
    tud_disconnect();

    // PIO USB port: host (keyboard / mouse); D+ is GPIO 12 and D- is GPIO 13
    pio_usb_configuration_t pio_config = PIO_USB_DEFAULT_CONFIG;
    pio_config.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
    tuh_configure( BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_config );

    tusb_rhport_init_t host_init = { .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO };
    tusb_init( BOARD_TUH_RHPORT, &host_init );

    // status light (after the system clock is set)
    led_init();

    kbd_reset();
    mouse_reset();

    while( true )
    {
        tuh_task();
        tud_task();
        gamepad_task();
        mode_task();
        report_task();
        status_task();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// USB device callbacks (gamepad side)
//
uint16_t tud_hid_get_report_cb( uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen )
{
    (void) instance; (void) report_id; (void) report_type;

    v32io_report_t report = { .buttons = buttons_sent, .x = 0, .y = 0 };

    if( reqlen < sizeof(report) )
      return 0;

    memcpy( buffer, &report, sizeof(report) );
    return sizeof(report);
}

void tud_hid_set_report_cb( uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize )
{
    (void) instance; (void) report_id; (void) report_type; (void) buffer; (void) bufsize;
}

//////////////////////////////////////////////////////////////////////////////
//
// USB host callbacks (keyboard / mouse side)
//

// called by the USB host stack on every event (maybe from an interrupt)
void tuh_event_hook_cb( uint8_t rhport, uint32_t eventid, bool in_isr )
{
    (void) rhport; (void) in_isr;

    if( eventid == HCD_EVENT_DEVICE_ATTACH ) attach_event = true;
    if( eventid == HCD_EVENT_DEVICE_REMOVE ) remove_event = true;
}

// a device has been completely set up: if by now it gave us no
// keyboard or mouse, it is something else (a hub, a gamepad...)
void tuh_mount_cb( uint8_t dev_addr )
{
    (void) dev_addr;
    connecting = false;
}

// a HID interface was connected: we only care for keyboards and mice.
// These are used in boot protocol (TinyUSB's default), so their reports
// always have the standard format
void tuh_hid_mount_cb( uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len )
{
    (void) desc_report; (void) desc_len;

    v32io_mode_t type = V32IO_MODE_NONE;

    switch( tuh_hid_interface_protocol( dev_addr, instance ) )
    {
        case HID_ITF_PROTOCOL_KEYBOARD: type = V32IO_MODE_KBD;   break;
        case HID_ITF_PROTOCOL_MOUSE:    type = V32IO_MODE_MOUSE; break;
        default:                        return;
    }

    if( add_input( dev_addr, instance, type ) )
      tuh_hid_receive_report( dev_addr, instance );

    // gamepad_task() will now choose the mode
}

// device unplugged: release everything it had pressed
void tuh_hid_umount_cb( uint8_t dev_addr, uint8_t instance )
{
    input_t* input = find_input( dev_addr, instance );
    if( !input ) return;

    if( input->type == V32IO_MODE_KBD )
    {
        hid_keyboard_report_t empty;
        memset( &empty, 0, sizeof(empty) );
        kbd_process_report( &input->last, &empty, mode == V32IO_MODE_KBD );
    }

    input->used = false;

    if( input->type == V32IO_MODE_MOUSE && mode == V32IO_MODE_MOUSE )
      mouse_input( all_mouse_buttons(), 0, 0 );

    // if this was the last device of the current mode,
    // gamepad_task() will change mode or unplug the gamepad
}

void tuh_hid_report_received_cb( uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len )
{
    input_t* input = find_input( dev_addr, instance );
    if( !input ) return;

    if( input->type == V32IO_MODE_KBD && len >= sizeof(hid_keyboard_report_t) )
    {
        hid_keyboard_report_t keys;
        memcpy( &keys, report, sizeof(keys) );
        kbd_process_report( &input->last, &keys, mode == V32IO_MODE_KBD );
    }

    // boot mouse reports: buttons, X and Y (and maybe more, not used)
    else if( input->type == V32IO_MODE_MOUSE && len >= 3 )
    {
        input->mouse_buttons = report[0];

        if( mode == V32IO_MODE_MOUSE )
          mouse_input( all_mouse_buttons(), (int8_t) report[1], (int8_t) report[2] );
    }

    // keep receiving reports
    tuh_hid_receive_report( dev_addr, instance );
}
