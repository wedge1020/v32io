#include "video.h"
#include "time.h"
#include "string.h"
#include "input.h"
#include "../storage.h"

//////////////////////////////////////////////////////////////////////////////
//
// storage test program: browse a file of the host computer.
//
// The storage device is expected in the FIRST channel of the memory card
// (channel 0). On the host, format the card and serve a file (see
// docs/v32dev/PROTOCOL.md); a text file works best in byte mode:
//
//     v32dev.py format game.memc --ch0 storage
//     v32dev.py serve  game.memc --ch0 storage:notes.txt,bytes
//
// and load that card in the emulator (with the v32dev patch).
//
//     UP / DOWN   previous / next page of the file
//     A           write a mark at the start of the page (if the device
//                 allows it), so the host file changes
//
#define PAGE_WORDS    1024
#define COLUMNS         64
#define MAX_ROWS        15
#define STORAGE_CHANNEL  0

void show_state (int state)
{
    if (state == V32DEV_NOCARD)
    {
        print_at (0, 40, "NO MEMORY CARD IS CONNECTED");
    }
    else if (state == V32DEV_NOEMU)
    {
        print_at (0, 40, "THE MEMORY CARD HAS NO LIVE v32dev MAILBOX");
        print_at (0, 80, "(format it with v32dev.py, and use the");
        print_at (0, 100, " emulator with the v32dev patch)");
    }
    else if (state == V32DEV_NOHOST)
    {
        print_at (0, 40, "WAITING FOR THE HOST (v32dev.py serve ...)");
    }
    else if (state == V32DEV_WRONG)
    {
        print_at (0, 40, "CHANNEL 0 IS NOT A BLOCK DEVICE");
    }
    else
    {
        print_at (0, 40, "STARTING...");
    }
}

void main (void)
{
    int             i         = 0;
    int             n         = 0;
    int             column    = 0;
    int             c         = 0;
    int             out       = 0;
    int             rows      = 0;
    int             page      = 0;
    int             pages     = 0;
    int             result    = 0;
    int             size      = 0;
    bool            loaded    = false;
    int [PAGE_WORDS]       words;
    int [PAGE_WORDS + 40]  text;
    int [12]        sym;
    int [32]        mark;
    storage        *st        = NULL;

    st                        = storage_init (STORAGE_CHANNEL);
    strcpy (mark, "<< written by Vircon32 >>");

    while (true)
    {
        //////////////////////////////////////////////////////////////////////
        //
        // Look at the device (do this once on every frame)
        //
        storage_probe (&st);
        clear_screen (color_black);

        if (!storage_ready (&st))
        {
            loaded            = false;
            print_at (0, 0, "STORAGE");
            show_state (v32dev_state (&(st -> dev)));
            end_frame ();
            continue;
        }

        size                  = storage_sector_words (&st) * storage_sectors (&st);
        pages                 = (size + PAGE_WORDS - 1) / PAGE_WORDS;

        //////////////////////////////////////////////////////////////////////
        //
        // Controls
        //
        select_gamepad (0);
        if (gamepad_down () == 1 && page < pages - 1)
        {
            page              = page + 1;
            loaded            = false;
        }
        if (gamepad_up () == 1 && page > 0)
        {
            page              = page - 1;
            loaded            = false;
        }
        if (gamepad_button_a () == 1)
        {
            result            = storage_write_words (&st, page * PAGE_WORDS, mark, strlen (mark));
            if (result       == STORAGE_OK)
            {
                result        = storage_flush (&st);
            }
            loaded            = false;
        }

        //////////////////////////////////////////////////////////////////////
        //
        // Read the page when needed
        //
        if (!loaded)
        {
            n                 = size - (page * PAGE_WORDS);
            if (n            >  PAGE_WORDS)
            {
                n             = PAGE_WORDS;
            }
            result            = storage_read_words (&st, page * PAGE_WORDS, words, n);

            // turn the words into text for the screen (only the low 8 bits
            // are a character), breaking the lines that are too long
            column            = 0;
            rows              = 0;
            out               = 0;
            for (i = 0; i < n && rows < MAX_ROWS; i = i + 1)
            {
                c             = words[i] & 255;
                if (c        == 10)
                {
                    text[out] = 10;
                    out       = out + 1;
                    column    = 0;
                    rows      = rows + 1;
                }
                else
                {
                    if (c    <  32 || c > 126)
                    {
                        c     = '.';
                    }
                    text[out] = c;
                    out       = out + 1;
                    column    = column + 1;
                    if (column == COLUMNS)
                    {
                        text[out] = 10;
                        out   = out + 1;
                        column  = 0;
                        rows  = rows + 1;
                    }
                }
            }
            text[out]         = 0;
            loaded            = true;
        }

        //////////////////////////////////////////////////////////////////////
        //
        // Draw the screen
        //
        print_at (0, 0, "PAGE");
        itoa (page + 1, sym, 10);
        print_at (50, 0, sym);
        print_at (100, 0, "OF");
        itoa (pages, sym, 10);
        print_at (130, 0, sym);
        itoa (storage_sector_words (&st), sym, 10);
        print_at (200, 0, "SECTOR:");
        print_at (280, 0, sym);
        if (storage_readonly (&st))
        {
            print_at (350, 0, "READ ONLY");
        }
        if (result       != STORAGE_OK)
        {
            itoa (result, sym, 10);
            print_at (450, 0, "ERROR");
            print_at (510, 0, sym);
        }
        print_at (0, 30, text);
        end_frame ();
    }
}
