#ifndef _SERIAL_H
#define _SERIAL_H

//////////////////////////////////////////////////////////////////////////////
//
// v32tools/v32dev library: "serial.h"           File version: 2026/10/15
// ----------------------------------------------------------------------
//
// This file contains all definitions and functions needed by programs to
// use a SERIAL device: a character device (class V32DEV_CLASS_CHAR, kind
// V32DEV_KIND_SERIAL) served by the host, like a terminal or a serial
// port. It is a plain two-way stream of words: the host decides what is at
// its end (this computer's terminal, a program, a network connection...).
//
// A word is a character (or whatever the program and the host agree on:
// the stream does not change the words). The host usually uses the lowest
// 8 bits of each word as a character.
//
// Reading the mailbox itself is done by the v32dev core (v32dev.h); this
// library only adds the conveniences of a terminal. Nothing blocks, except
// the functions that say so (serial_wait_ready and serial_write_all).
//
// IMPORTANT: serial_probe() must be called once on EVERY frame.
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//
// include the v32dev core (it includes the needed DevTools headers)
//
#include "v32dev.h"

//////////////////////////////////////////////////////////////////////////////
//
// serial is a serial device on a channel
//
struct serial
{
    v32dev *dev;             // the channel
    bool    ready;           // the channel is ready, as of the latest probe
};

//////////////////////////////////////////////////////////////////////////////
//
// API: function prototypes for the functions in this library
//
serial *serial_init         (int channel);                    // new instance
void    serial_free         (serial **sp);                    // release it
bool    serial_probe        (serial **sp);                    // once per frame
bool    serial_wait_ready   (serial **sp, int frames);        // wait for host
bool    serial_ready        (serial **sp);                    // usable now?
bool    serial_hup          (serial **sp);                    // host is gone?
int     serial_avail        (serial **sp);                    // words to read
int     serial_space        (serial **sp);                    // words that fit
int     serial_read         (serial **sp, int *buffer, int count);
int     serial_write        (serial **sp, int *buffer, int count);
int     serial_write_all    (serial **sp, int *buffer, int count);
int     serial_getc         (serial **sp);                    // -1: nothing
bool    serial_putc         (serial **sp, int character);     // false: full
int     serial_puts         (serial **sp, int *text);         // string.h text

//////////////////////////////////////////////////////////////////////////////
//
// serial_init(): initialize a new serial device on a channel (0 to 3). It
// is usable after a probe says so (see serial_wait_ready).
//
serial *serial_init (int channel)
{
    serial *sp                = NULL;

    sp                        = (serial *) malloc (sizeof (serial) * 1);
    if (sp                   != NULL)
    {
        sp -> dev             = v32dev_init (channel, V32DEV_CLASS_CHAR);
        sp -> ready           = false;

        if (sp -> dev        == NULL)
        {
            free (sp);
            sp                = NULL;
        }
    }

    return (sp);
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_free(): release a serial instance
//
void    serial_free (serial **sp)
{
    if (*sp                  != NULL)
    {
        v32dev_free (&((*sp) -> dev));
        free (*sp);
        *sp                   = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_probe(): look at the device for this frame; call it once per
// frame. Returns true when the device became or stopped being ready.
//
bool    serial_probe (serial **sp)
{
    bool    before            = false;

    if (*sp                  == NULL)
    {
        return (false);
    }

    before                    = (*sp) -> ready;
    v32dev_probe (&((*sp) -> dev));
    (*sp) -> ready            = v32dev_ready (&((*sp) -> dev));

    return ((*sp) -> ready != before);
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_wait_ready(): end frames, probing each time, until the device is
// usable. Gives up after the given number of frames (returns false).
//
bool    serial_wait_ready (serial **sp, int frames)
{
    serial_probe (sp);
    while (*sp               != NULL && !(*sp) -> ready && frames > 0)
    {
        frames                = frames - 1;
        end_frame ();
        serial_probe (sp);
    }

    return (*sp != NULL && (*sp) -> ready);
}

bool    serial_ready (serial **sp)
{
    return (*sp != NULL && (*sp) -> ready);
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_hup(): did the host hang up (it says there will be no more
// words), and has everything it sent been read?
//
bool    serial_hup (serial **sp)
{
    if (*sp                  == NULL)
    {
        return (true);
    }
    return (v32dev_ready (&((*sp) -> dev))
         && (v32dev_status (&((*sp) -> dev)) & V32DEV_ST_HUP) != 0
         && v32dev_avail (&((*sp) -> dev)) == 0);
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_avail(): words that can be read now. serial_space(): words that
// can be written now.
//
int     serial_avail (serial **sp)
{
    if (*sp                  == NULL)
    {
        return (0);
    }
    return (v32dev_avail (&((*sp) -> dev)));
}

int     serial_space (serial **sp)
{
    if (*sp                  == NULL)
    {
        return (0);
    }
    return (v32dev_space (&((*sp) -> dev)));
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_read(): take up to count words (returns how many, maybe 0).
// serial_write(): give up to count words (returns how many were taken,
// maybe 0, if the host is not keeping up).
//
int     serial_read (serial **sp, int *buffer, int count)
{
    if (*sp                  == NULL)
    {
        return (0);
    }
    return (v32dev_read (&((*sp) -> dev), buffer, count));
}

int     serial_write (serial **sp, int *buffer, int count)
{
    if (*sp                  == NULL)
    {
        return (0);
    }
    return (v32dev_write (&((*sp) -> dev), buffer, count));
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_write_all(): give all count words, ending frames while the host
// makes room. Gives up, returning how many words were taken, if the device
// stops being ready.
//
int     serial_write_all (serial **sp, int *buffer, int count)
{
    int     done              = 0;
    int     part              = 0;

    while (done              <  count && serial_ready (sp))
    {
        part                  = serial_write (sp, buffer + done, count - done);
        done                  = done + part;
        if (done             <  count)
        {
            end_frame ();
            serial_probe (sp);
        }
    }

    return (done);
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_getc(): take one word, or -1 if there is none
//
int     serial_getc (serial **sp)
{
    int [1] word;

    if (serial_read (sp, &word[0], 1) == 1)
    {
        return (word[0]);
    }
    return (-1);
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_putc(): give one word; false if the host has no room for it
//
bool    serial_putc (serial **sp, int character)
{
    int [1] word;

    word[0]                   = character;
    return (serial_write (sp, &word[0], 1) == 1);
}

//////////////////////////////////////////////////////////////////////////////
//
// serial_puts(): give a text (an array of words ended by 0, as in string.h;
// the 0 is not sent). Waits for room, as serial_write_all(). Returns how
// many words were sent.
//
int     serial_puts (serial **sp, int *text)
{
    int     length            = 0;

    while (text[length]      != 0)
    {
        length                = length + 1;
    }

    return (serial_write_all (sp, text, length));
}

#endif
