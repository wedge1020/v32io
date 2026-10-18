#include "mouse.h"

// results for the test harness: [0],[1] magic, then data
int [32] results;

void main (void)
{
    v32mouse *mouse = NULL;
    int  absx = 0;
    int  absy = 0;

    results[0] = 0x7E57AB1E;
    results[1] = 0x0B5E55ED;
    mouse      = v32mouse_init (SECOND_GAMEPAD_PORT);
    v32mouse_setscale (&mouse, 1);
    v32mouse_setbounds (&mouse, -1000000, -1000000, 1000000, 1000000);
    v32mouse_setposition (&mouse, 0, 0);

    while (true)
    {
        v32mouse_probe (&mouse);

        results[2]  = results[2] + 1;                 // frames
        results[3]  = results[3] + mouse -> stepx;    // total steps x
        results[4]  = results[4] + mouse -> stepy;    // total steps y
        results[5]  = mouse -> buttons;
        if (v32mouse_pressed  (&mouse, V32MOUSE_LEFT))   results[6] = results[6] + 1;
        if (v32mouse_released (&mouse, V32MOUSE_LEFT))   results[7] = results[7] + 1;
        if (v32mouse_pressed  (&mouse, V32MOUSE_RIGHT))  results[8] = results[8] + 1;
        if (v32mouse_pressed  (&mouse, V32MOUSE_MIDDLE)) results[9] = results[9] + 1;
        absx = mouse -> stepx; if (absx < 0) absx = -absx;
        absy = mouse -> stepy; if (absy < 0) absy = -absy;
        if (absx > results[10]) results[10] = absx;   // max steps in a frame
        if (absy > results[10]) results[10] = absy;
        if (mouse -> countx < 0 || mouse -> county < 0) results[11] = results[11] + 1;  // invalid
        results[12] = mouse -> x;
        results[13] = mouse -> y;
        results[14] = mouse -> countx;
        results[15] = mouse -> county;
        end_frame ();
    }
}
