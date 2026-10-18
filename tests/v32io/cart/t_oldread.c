#include "keyboard_20261008.h"

// A program built with the keyboard library as it was BEFORE extended
// characters (compat/keyboard_20261008.h), reading with v32kbd_read().
// results: [0],[1] magic, [2] frames, [3] count, [4..] values
int [2052] results;

void main (void)
{
    v32kbd *keyboard = NULL;
    int     value    = 0;

    results[0] = 0x7E57AB1E;
    results[1] = 0x0B5E55ED;
    keyboard   = v32kbd_init (SECOND_GAMEPAD_PORT);

    while (true)
    {
        v32kbd_probe (&keyboard);
        results[2] = results[2] + 1;

        value      = v32kbd_read (&keyboard);
        while (value > 0)
        {
            if (results[3] < 2048)
            {
                results[4 + results[3]] = value;
                results[3] = results[3] + 1;
            }
            value  = v32kbd_read (&keyboard);
        }
        end_frame ();
    }
}
