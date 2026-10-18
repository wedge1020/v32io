#include "keyboard.h"

// results: [0],[1] magic, [2] frames, [3] count, [4..2051] values returned
// by v32kbd_read(), [2052..] v32kbd_glyph() of each of them
int [4104] results;

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
                results[4 + results[3]]    = value;
                results[2052 + results[3]] = v32kbd_glyph (value);
                results[3] = results[3] + 1;
            }
            value  = v32kbd_read (&keyboard);
        }
        end_frame ();
    }
}
