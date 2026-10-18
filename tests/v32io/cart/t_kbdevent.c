#include "keyboard.h"

// results: [0],[1] magic, [2] frames, [3] event count, [4..] events as
// v32kbd_readevent() returns them (key code, +256 if pressed). Typed
// characters are not keys and must not show up here.
int [2052] results;

void main (void)
{
    v32kbd *keyboard = NULL;
    bool    pressed  = false;
    int     key      = 0;

    results[0] = 0x7E57AB1E;
    results[1] = 0x0B5E55ED;
    keyboard   = v32kbd_init (SECOND_GAMEPAD_PORT);

    while (true)
    {
        v32kbd_probe (&keyboard);
        results[2] = results[2] + 1;

        key        = v32kbd_readevent (&keyboard, &pressed);
        while (key > 0)
        {
            if (results[3] < 2048)
            {
                results[4 + results[3]] = key;
                if (pressed) results[4 + results[3]] = key + 256;
                results[3] = results[3] + 1;
            }
            key    = v32kbd_readevent (&keyboard, &pressed);
        }
        end_frame ();
    }
}
