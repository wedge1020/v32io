#include "video.h"
#include "string.h"
#include "serial.h"

//////////////////////////////////////////////////////////////////////////////
//
// serial.h test program, run by dev_rom_test.py in the (patched) emulator
// code against a host that echoes (serial:echo). It sends a lot more words
// than the ring holds, reading the echo while it goes, and checks them.
// The report is written to words 65 to 112 of the card.
//
#define REPORT_WORD      65
#define REPORT_SIZE      48
#define TOTAL          5000

int pattern (int i)
{
    return ((i << 8) ^ 0x80000003 ^ i);
}

int [REPORT_SIZE] rep;
int [256] tx;
int [512] rx;

void main (void)
{
    int         i             = 0;
    int         n             = 0;
    int         sent          = 0;
    int         got           = 0;
    int         bad           = 0;
    int         frames        = 0;
    int         word          = 0;
    int [8]     text;
    serial     *sp            = NULL;

    for (i = 0; i < REPORT_SIZE; i = i + 1)
    {
        rep[i]                = 0;
    }
    rep[0]                    = 0x54504552;                // "REPT"

    sp                        = serial_init (2);
    rep[2]                    = serial_wait_ready (&sp, 400);
    rep[3]                    = v32dev_state (&(sp -> dev));
    rep[4]                    = v32dev_class (&(sp -> dev));
    rep[5]                    = v32dev_kind (&(sp -> dev));

    if (rep[2])
    {
        //////////////////////////////////////////////////////////////////////
        //
        // Many more words than a ring holds, in both directions at once
        //
        while ((sent < TOTAL || got < TOTAL) && frames < 4000 && serial_ready (&sp))
        {
            if (sent < TOTAL)
            {
                n             = TOTAL - sent;
                if (n > 256)
                {
                    n         = 256;
                }
                for (i = 0; i < n; i = i + 1)
                {
                    tx[i]     = pattern (sent + i);
                }
                sent          = sent + serial_write (&sp, tx, n);
            }
            n                 = serial_read (&sp, rx, 512);
            for (i = 0; i < n; i = i + 1)
            {
                if (rx[i] != pattern (got + i))
                {
                    bad       = bad + 1;
                }
            }
            got               = got + n;
            end_frame ();
            serial_probe (&sp);
            frames            = frames + 1;
        }
        rep[6]                = sent;
        rep[7]                = got;
        rep[8]                = bad;
        rep[9]                = frames;

        //////////////////////////////////////////////////////////////////////
        //
        // A text, and single words
        //
        strcpy (text, "hello");
        rep[10]               = serial_puts (&sp, text);
        serial_putc (&sp, 90);
        i                     = 0;
        n                     = 0;
        while (n < 6 && i < 400)
        {
            word              = serial_getc (&sp);
            if (word         >= 0)
            {
                rep[16 + n]   = word;
                n             = n + 1;
            }
            else
            {
                end_frame ();
                serial_probe (&sp);
                i             = i + 1;
            }
        }
        rep[11]               = n;

        //////////////////////////////////////////////////////////////////////
        //
        // The host hangs up after everything has been echoed
        //
        i                     = 0;
        while (!serial_hup (&sp) && i < 400)
        {
            end_frame ();
            serial_probe (&sp);
            i                 = i + 1;
        }
        rep[12]               = serial_hup (&sp);
        rep[13]               = serial_read (&sp, rx, 4);          // nothing more comes
    }

    rep[1]                    = 99;
    card_write_data (rep, REPORT_WORD, REPORT_SIZE);
    serial_free (&sp);
}
