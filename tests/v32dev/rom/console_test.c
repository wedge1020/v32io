#include "video.h"
#include "string.h"
#include "serial.h"

//////////////////////////////////////////////////////////////////////////////
//
// serial.h "console" test program, run by dev_rom_test.py in the (patched)
// emulator code against the built in v32dev server, with serial:stdio or
// serial:tcp on the channel written in word 64 of the card.
//
// Like a login prompt, it shows READY until someone types (output before
// anyone listens is lost, as on any serial line), reads a line, answers
// "echo:" and the line, and waits for the other end to hang up.
// The report is written to words 65 to 112 of the card.
//
#define CHANNEL_WORD     64
#define REPORT_WORD      65
#define REPORT_SIZE      48

int [REPORT_SIZE] rep;
int [64] line;
int [16] text;

void main (void)
{
    int         i             = 0;
    int         n             = 0;
    int         frames        = 0;
    int         word          = 0;
    int         done          = 0;
    int         channel       = 0;
    serial     *sp            = NULL;

    for (i = 0; i < REPORT_SIZE; i = i + 1)
    {
        rep[i]                = 0;
    }
    rep[0]                    = 0x54504552;                // "REPT"

    card_read_data (&channel, CHANNEL_WORD, 1);
    sp                        = serial_init (channel);
    rep[2]                    = serial_wait_ready (&sp, 400);
    rep[3]                    = v32dev_class (&(sp -> dev));

    if (rep[2])
    {
        //////////////////////////////////////////////////////////////////////
        //
        // The prompt, again and again, until a line arrives
        //
        while (!done && frames < 3000 && serial_ready (&sp))
        {
            if (frames % 20 == 0 && n == 0)
            {
                strcpy (text, "READY\n");
                serial_puts (&sp, text);
            }
            word              = serial_getc (&sp);
            while (word      >= 0 && n < 60)
            {
                line[n]       = word;
                n             = n + 1;
                if (word == 10)
                {
                    done      = 1;
                }
                word          = serial_getc (&sp);
            }
            end_frame ();
            serial_probe (&sp);
            frames            = frames + 1;
        }
        rep[4]                = n;
        rep[5]                = done;
        for (i = 0; i < n && i < 16; i = i + 1)
        {
            rep[16 + i]       = line[i];
        }

        //////////////////////////////////////////////////////////////////////
        //
        // The answer
        //
        if (done)
        {
            strcpy (text, "echo:");
            serial_puts (&sp, text);
            serial_write_all (&sp, line, n);
        }

        //////////////////////////////////////////////////////////////////////
        //
        // The other end hangs up
        //
        i                     = 0;
        while (!serial_hup (&sp) && i < 1500)
        {
            end_frame ();
            serial_probe (&sp);
            i                 = i + 1;
        }
        rep[6]                = serial_hup (&sp);
    }

    rep[1]                    = 99;
    card_write_data (rep, REPORT_WORD, REPORT_SIZE);
    serial_free (&sp);
}
