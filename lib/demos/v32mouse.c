#include "video.h"
#include "time.h"
#include "string.h"
#include "../mouse.h"

//////////////////////////////////////////////////////////////////////////////
//
// v32mouse test program: draw on screen with the mouse.
//
//   left button:   hold to draw
//   right button:  clear the drawing
//   middle button: change the drawing color
//
// The mouse is expected in the FOURTH gamepad port (id 3): select the
// "v32io:mouse" joystick profile for Gamepad 4 in the emulator
//
#define DOTS_MAX  500
#define COLORS      4

void main (void)
{
    int             index     = 0;
    int             x         = 0;
    int             y         = 0;
    int             dx        = 0;
    int             dy        = 0;
    int             dots      = 0;
    int             color     = 0;
    int [DOTS_MAX]  dotx;
    int [DOTS_MAX]  doty;
    int [DOTS_MAX]  dotcolor;
    int [COLORS]    palette;
    int [12]        number;
    v32mouse       *mouse     = NULL;

    palette[0]                = color_white;
    palette[1]                = color_red;
    palette[2]                = color_green;
    palette[3]                = color_yellow;

    mouse                     = v32mouse_init (FOURTH_GAMEPAD_PORT);

    while (true)
    {
        //////////////////////////////////////////////////////////////////////
        //
        // Check for mouse activity (do this once on every frame)
        //
        v32mouse_probe (&mouse);
        v32mouse_position (&mouse, &x, &y);

        //////////////////////////////////////////////////////////////////////
        //
        // Movement in this frame: only update the shown values when the
        // mouse moved, so that they can be read
        //
        if (mouse -> dx != 0 || mouse -> dy != 0)
        {
            v32mouse_delta (&mouse, &dx, &dy);
        }

        //////////////////////////////////////////////////////////////////////
        //
        // Buttons
        //
        if (v32mouse_pressed (&mouse, V32MOUSE_RIGHT))
        {
            dots              = 0;
        }

        if (v32mouse_pressed (&mouse, V32MOUSE_MIDDLE))
        {
            color             = (color + 1) % COLORS;
        }

        if (v32mouse_isdown (&mouse, V32MOUSE_LEFT) && (dots < DOTS_MAX))
        {
            dotx[dots]        = x;
            doty[dots]        = y;
            dotcolor[dots]    = palette[color];
            dots              = dots + 1;
        }

        //////////////////////////////////////////////////////////////////////
        //
        // Draw the screen
        //
        clear_screen (color_black);

        for (index            = 0; index < dots; index = index + 1)
        {
            set_multiply_color (dotcolor[index]);
            print_at (dotx[index] - 5, doty[index] - 10, "*");
        }

        set_multiply_color (color_white);
        print_at (0, 0, "X:      Y:      DX:     DY:");
        itoa (x,  number, 10);  print_at ( 30, 0, number);
        itoa (y,  number, 10);  print_at (110, 0, number);
        itoa (dx, number, 10);  print_at (200, 0, number);
        itoa (dy, number, 10);  print_at (280, 0, number);

        print_at (360, 0, "[L] [M] [R]");
        if (v32mouse_isdown (&mouse, V32MOUSE_LEFT))   print_at (370, 0, "*");
        if (v32mouse_isdown (&mouse, V32MOUSE_MIDDLE)) print_at (410, 0, "*");
        if (v32mouse_isdown (&mouse, V32MOUSE_RIGHT))  print_at (450, 0, "*");

        set_multiply_color (palette[color]);
        print_at (510, 0, "COLOR");

        set_multiply_color (color_cyan);
        print_at (x - 5, y - 10, "+");

        end_frame ();
    }
}
