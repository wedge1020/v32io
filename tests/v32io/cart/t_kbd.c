#include "keyboard.h"

// results: [0],[1] magic, [2] frames, [3] event count, [4..] events
// each event: key code, +256 if it is a press; +512 if it is a typed
// character (extended characters) instead of a key, with its BIOS code.
// Reads everything the library received, with v32kbd_getkey().
int [2052] results;

void main (void)
{
    v32kbd *keyboard = NULL;
    v32key *key      = NULL;
    int     value    = 0;

    results[0] = 0x7E57AB1E;
    results[1] = 0x0B5E55ED;
    keyboard   = v32kbd_init (SECOND_GAMEPAD_PORT);

    while (true)
    {
        v32kbd_probe (&keyboard);
        results[2] = results[2] + 1;

        key        = v32kbd_getkey (&keyboard);
        while (key != NULL)
        {
            value  = key -> value;
            if (key -> text)
            {
                value = value + 512;
            }
            else if (key -> pressed)
            {
                value = value + 256;
            }

            if (results[3] < 2048)
            {
                results[4 + results[3]] = value;
                results[3] = results[3] + 1;
            }
            free (key);
            key    = v32kbd_getkey (&keyboard);
        }
        end_frame ();
    }
}
