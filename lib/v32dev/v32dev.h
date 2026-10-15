#ifndef _V32DEV_H
#define _V32DEV_H

//////////////////////////////////////////////////////////////////////////////
//
// v32tools/v32dev library: "v32dev.h"           File version: 2026/10/15
// ----------------------------------------------------------------------
//
// This file is the core of the v32dev device family (storage, serial,
// network, netplay...). v32dev lets a program exchange data with the
// outside world through the memory card: a patched emulator keeps a
// "mailbox" region of the card file synchronized with an external host
// program (see docs/v32dev/PROTOCOL.md). The program only reads and writes
// card memory; no I/O ports are involved.
//
// The mailbox has 4 CHANNELS. A channel is a pair of rings (a "pipe" in
// each direction) plus control words, and the host decides what is at the
// other end of it, in the manner of unix devices:
//
//   CLASS        what it is like                    used by
//   -----------  ---------------------------------  ----------------
//   block   (1)  fixed-size sectors, random access  storage.h
//   char    (2)  stream of words, like a terminal   serial.h
//   pipe    (3)  one-way stream, with end of file   (reserved)
//   socket  (4)  connections and datagrams          network.h, netplay.h
//
// Each class has KINDS (for example, block:storage), and 128 and above are
// free for custom classes. The class and kind of a channel are announced
// by the host as a single "type" value (V32DEV_TYPE).
//
// v32dev itself knows nothing about what the data means. It provides:
//
//   - finding out whether the channel is usable (v32dev_probe, once per
//     frame; v32dev_state, v32dev_ready, v32dev_events)
//   - streams of words (v32dev_read, v32dev_write)
//   - messages (v32dev_send, v32dev_recv): a stream with boundaries,
//     published all at once, used by the classes that do requests
//
// Device drivers (storage.h, serial.h...) own a v32dev instance, call
// v32dev_probe() once per frame, and then use these functions.
//
// TIMING: the host only sees what the program wrote at the end of a frame,
// and the program only sees what the host wrote at the start of a frame.
// So a request and its reply take at least 1 frame (when the host answers
// before the next frame begins), and more if the host is slow. Throughput
// is limited by the ring size (2023 words per channel and direction, per
// frame).
//
// IMPORTANT: v32dev_probe() must be called once on EVERY frame for each
// channel that is in use.
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//
// include needed Vircon32 DevTools headers
//
#include "memcard.h"
#include "misc.h"
#include "time.h"

//////////////////////////////////////////////////////////////////////////////
//
// layout of the mailbox (card memory addresses; see docs/v32dev/PROTOCOL.md)
//
#define V32DEV_HEADER          0x3003C000  // header: 64 words
#define V32DEV_HOST_AREA       0x3003C040  // written by the host, 4 parts
#define V32DEV_CART_AREA       0x3003E020  // written by the program, 4 parts
#define V32DEV_MAGIC           0x31564544  // header word 0: bytes "DEV1"
#define V32DEV_VERSION                  1  // header word 1
#define V32DEV_CHANNELS                 4  // number of channels
#define V32DEV_PART_WORDS            2040  // size of a part, in words
#define V32DEV_CONTROL_WORDS           16  // control words at the part start
#define V32DEV_RING_WORDS            2024  // slots of a ring (one is never used)
#define V32DEV_MSG_MAX               2020  // most payload words in a message

// header words
#define V32DEV_HDR_MAGIC                0
#define V32DEV_HDR_VERSION              1
#define V32DEV_HDR_CHANNELS             3
#define V32DEV_HDR_PART_WORDS           4
#define V32DEV_HDR_EMU_FLAGS            8  // memory only, set by the emulator
#define V32DEV_HDR_EMU_BEAT             9  // memory only, +1 on every frame

// control words of a host part (the program only reads them)
#define V32DEV_H_TYPE                   0  // class and kind (V32DEV_TYPE)
#define V32DEV_H_FLAGS                  1  // bit 0: a host is attached
#define V32DEV_H_BEAT                   2  // host heartbeat
#define V32DEV_H_H2C_HEAD               3  // write index of host to program
#define V32DEV_H_C2H_TAIL               4  // read index of program to host
#define V32DEV_H_SESSION                5  // changes each time the host starts
#define V32DEV_H_STATUS                 6  // V32DEV_ST_ bits
#define V32DEV_H_DESC                   8  // device descriptor: 8 words

// control words of a cart part (the host only reads them)
#define V32DEV_C_FLAGS                  0  // bit 0: program has opened it
#define V32DEV_C_C2H_HEAD               1  // write index of program to host
#define V32DEV_C_H2C_TAIL               2  // read index of host to program
#define V32DEV_C_BEAT                   3  // optional heartbeat
#define V32DEV_C_SESSION                4  // +1 each time the program opens
#define V32DEV_C_STATUS                 5  // V32DEV_ST_ bits
#define V32DEV_C_DESC                   8  // device request words: 8 words

//////////////////////////////////////////////////////////////////////////////
//
// device classes and kinds. The type announced by a host is
// (class << 24) | kind.
//
#define V32DEV_CLASS_NONE               0
#define V32DEV_CLASS_BLOCK              1
#define V32DEV_CLASS_CHAR               2
#define V32DEV_CLASS_PIPE               3
#define V32DEV_CLASS_SOCKET             4
#define V32DEV_CLASS_CUSTOM           128  // 128 to 255: free for custom use

#define V32DEV_KIND_ANY                 0  // generic device of the class

#define V32DEV_KIND_STORAGE             1  // block: disk image on the host
#define V32DEV_KIND_SERIAL              1  // char: serial port / stdio
#define V32DEV_KIND_STREAM              1  // pipe: stream to a host process
#define V32DEV_KIND_TCP                 1  // socket: connections
#define V32DEV_KIND_UDP                 2  // socket: datagrams
#define V32DEV_KIND_NETPLAY             3  // socket: netplay sessions

#define V32DEV_TYPE(class, kind)  (((class) << 24) | (kind))

// bits of the status words (host status: V32DEV_H_STATUS)
#define V32DEV_ST_READY                 1  // device is usable
#define V32DEV_ST_ERROR                 2  // device has failed
#define V32DEV_ST_HUP                   4  // other end is gone: no more data
                                           // once the ring has been emptied

//////////////////////////////////////////////////////////////////////////////
//
// states of a channel (see v32dev_state)
//
#define V32DEV_NOCARD                   0  // no memory card is connected
#define V32DEV_NOEMU                    1  // card has no live mailbox: it is
                                           // not formatted, or the emulator
                                           // does not have the patch
#define V32DEV_NOHOST                   2  // mailbox is live, nobody is
                                           // serving this channel
#define V32DEV_WRONG                    3  // a device of another class
#define V32DEV_READY                    4  // channel can be used

// events since the previous probe (see v32dev_events)
#define V32DEV_EV_OPEN                  1  // channel just became ready
#define V32DEV_EV_LOST                  2  // channel stopped being ready
#define V32DEV_EV_RESET                 4  // host restarted: rings were emptied

//////////////////////////////////////////////////////////////////////////////
//
// library parameters
//
#define V32DEV_EMU_STILL                3  // frames without emulator beat
#define V32DEV_HOST_STILL             120  // frames without host heartbeat
                                           // before it is taken as gone

//////////////////////////////////////////////////////////////////////////////
//
// v32dev is a channel of the mailbox
//
struct v32dev
{
    int     channel;    // channel number (0 to 3)
    int     expect;     // class the program expects (0: any)
    int     state;      // V32DEV_ state, from the latest probe
    int     events;     // V32DEV_EV_ events of the latest probe
    int     frame;      // frame counter at the latest probe (-1: none yet)
    int     emu_beat;   // latest emulator heartbeat seen
    int     emu_still;  // probes in a row with the same emulator heartbeat
    int     host_beat;  // latest host heartbeat seen
    int     host_still; // probes in a row with the same host heartbeat
    int     session;    // host session when the channel became ready
    int     errors;     // ring indices found corrupt (they were emptied)
    int    *header;     // the mailbox header in card memory
    int    *host;       // host part of this channel
    int    *cart;       // cart part of this channel
};

//////////////////////////////////////////////////////////////////////////////
//
// API: function prototypes for the functions in this library
//
v32dev *v32dev_init      (int channel, int class);    // initialize instance
void    v32dev_free      (v32dev **dev);              // close and release
bool    v32dev_probe     (v32dev **dev);              // once per frame
bool    v32dev_wait      (v32dev **dev, int frames);  // wait until ready
int     v32dev_state     (v32dev **dev);              // V32DEV_ state
bool    v32dev_ready     (v32dev **dev);              // usable now?
int     v32dev_events    (v32dev **dev);              // V32DEV_EV_ events
int     v32dev_type      (v32dev **dev);              // announced type
int     v32dev_class     (v32dev **dev);              // announced class
int     v32dev_kind      (v32dev **dev);              // announced kind
int     v32dev_status    (v32dev **dev);              // V32DEV_ST_ bits
int     v32dev_desc      (v32dev **dev, int index);   // descriptor word 0-7
void    v32dev_setdesc   (v32dev **dev, int index, int value);
int     v32dev_avail     (v32dev **dev);              // words to read
int     v32dev_space     (v32dev **dev);              // words that fit
int     v32dev_read      (v32dev **dev, int *buffer, int count);
int     v32dev_write     (v32dev **dev, int *buffer, int count);
bool    v32dev_send      (v32dev **dev, int opcode, int *payload, int count);
bool    v32dev_sendv     (v32dev **dev, int opcode, int *first, int firstcount,
                          int *second, int secondcount);
int     v32dev_next      (v32dev **dev);              // size of next message
int     v32dev_recv      (v32dev **dev, int *opcode, int *payload, int max);

//////////////////////////////////////////////////////////////////////////////
//
// internal: copy words out of / into a ring (with wrap-around). These need
// count > 0, and a slot number from 0 to V32DEV_RING_WORDS - 1
//
void    v32dev_ring_get (int *ring, int slot, int *buffer, int count)
{
    int     first             = V32DEV_RING_WORDS - slot;

    if (first                >  count)
    {
        first                 = count;
    }
    memcpy (buffer, ring + slot, first);
    if (count                >  first)
    {
        memcpy (buffer + first, ring, count - first);
    }
}

void    v32dev_ring_put (int *ring, int slot, int *buffer, int count)
{
    int     first             = V32DEV_RING_WORDS - slot;

    if (first                >  count)
    {
        first                 = count;
    }
    memcpy (ring + slot, buffer, first);
    if (count                >  first)
    {
        memcpy (ring, buffer + first, count - first);
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// internal: take the state of the channel from the card (one per frame)
//
void    v32dev_refresh (v32dev *d)
{
    int     before            = d -> state;
    int     magic             = 0;
    int     beat              = 0;
    int     type              = 0;
    int     tail              = 0;
    int     head              = 0;
    bool    live              = false;
    bool    attached          = false;

    d -> events               = 0;

    //////////////////////////////////////////////////////////////////////////
    //
    // Reading card memory without a card is a hardware error, so the card
    // is checked first
    //
    if (!card_is_connected ())
    {
        d -> state            = V32DEV_NOCARD;
    }
    else
    {
        //////////////////////////////////////////////////////////////////////
        //
        // The mailbox is live when the header is valid AND the emulator
        // heartbeat (which is in memory only) keeps changing: the header
        // alone can be read from a card on an emulator without the patch
        //
        magic                 = d -> header[V32DEV_HDR_MAGIC];
        beat                  = d -> header[V32DEV_HDR_EMU_BEAT];
        if (beat             != d -> emu_beat)
        {
            d -> emu_beat     = beat;
            d -> emu_still    = 0;
        }
        else
        {
            d -> emu_still    = d -> emu_still + 1;
        }

        live                  = (magic == V32DEV_MAGIC
                              && d -> header[V32DEV_HDR_VERSION] == V32DEV_VERSION
                              && d -> header[V32DEV_HDR_CHANNELS] == V32DEV_CHANNELS
                              && d -> header[V32DEV_HDR_PART_WORDS] == V32DEV_PART_WORDS
                              && d -> emu_still < V32DEV_EMU_STILL);
        if (!live)
        {
            d -> state        = V32DEV_NOEMU;
        }
        else
        {
            //////////////////////////////////////////////////////////////////
            //
            // Is a host serving this channel? It must say so, AND keep
            // updating its heartbeat
            //
            beat              = d -> host[V32DEV_H_BEAT];
            if (beat         != d -> host_beat)
            {
                d -> host_beat  = beat;
                d -> host_still = 0;
            }
            else
            {
                d -> host_still = d -> host_still + 1;
            }

            type              = d -> host[V32DEV_H_TYPE];
            attached          = ((d -> host[V32DEV_H_FLAGS] & 1) != 0
                              && d -> host_still < V32DEV_HOST_STILL
                              && type != 0);
            if (!attached)
            {
                d -> state    = V32DEV_NOHOST;
            }
            else if (d -> expect != 0
                  && ((type >> 24) & 255) != d -> expect)
            {
                d -> state    = V32DEV_WRONG;
            }
            else
            {
                d -> state    = V32DEV_READY;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Becoming ready opens the channel: whatever was left in the rings
    // by an earlier run is discarded (each side moves the end it owns),
    // and the host can tell the program has opened it again
    //
    if (d -> state           == V32DEV_READY)
    {
        if (before           != V32DEV_READY)
        {
            head              = d -> host[V32DEV_H_H2C_HEAD];
            tail              = d -> host[V32DEV_H_C2H_TAIL];
            if (head         >= 0 && head < V32DEV_RING_WORDS)
              d -> cart[V32DEV_C_H2C_TAIL] = head;
            else
              d -> cart[V32DEV_C_H2C_TAIL] = 0;
            if (tail         >= 0 && tail < V32DEV_RING_WORDS)
              d -> cart[V32DEV_C_C2H_HEAD] = tail;
            else
              d -> cart[V32DEV_C_C2H_HEAD] = 0;
            d -> cart[V32DEV_C_FLAGS]    = 1;
            d -> cart[V32DEV_C_SESSION]  = d -> cart[V32DEV_C_SESSION] + 1;
            d -> session      = d -> host[V32DEV_H_SESSION];
            d -> events       = d -> events | V32DEV_EV_OPEN;
        }
        else if (d -> host[V32DEV_H_SESSION] != d -> session)
        {
            //////////////////////////////////////////////////////////////////
            //
            // The host was restarted: anything in the rings is stale
            //
            head              = d -> host[V32DEV_H_H2C_HEAD];
            tail              = d -> host[V32DEV_H_C2H_TAIL];
            if (head         >= 0 && head < V32DEV_RING_WORDS)
              d -> cart[V32DEV_C_H2C_TAIL] = head;
            if (tail         >= 0 && tail < V32DEV_RING_WORDS)
              d -> cart[V32DEV_C_C2H_HEAD] = tail;
            d -> session      = d -> host[V32DEV_H_SESSION];
            d -> events       = d -> events | V32DEV_EV_RESET;
        }
    }
    else if (before          == V32DEV_READY)
    {
        d -> events           = d -> events | V32DEV_EV_LOST;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_init(): initialize a new instance for a channel (0 to 3). The
// class says what the program expects to find there (V32DEV_CLASS_...,
// or 0 to accept anything). The channel is not usable until a probe says
// it is ready (v32dev_wait can be used to wait for that).
//
v32dev *v32dev_init (int channel, int class)
{
    v32dev *d                 = NULL;
    int     offset            = 0;

    if (channel              <  0
     || channel              >= V32DEV_CHANNELS)
    {
        return (NULL);
    }

    d                         = (v32dev *) malloc (sizeof (v32dev) * 1);
    if (d                    != NULL)
    {
        offset                = channel * V32DEV_PART_WORDS;
        d -> channel          = channel;
        d -> expect           = class;
        d -> state            = V32DEV_NOCARD;
        d -> events           = 0;
        d -> frame            = -1;
        d -> emu_beat         = 0;
        d -> emu_still        = V32DEV_EMU_STILL;
        d -> host_beat        = 0;
        d -> host_still       = V32DEV_HOST_STILL;
        d -> session          = 0;
        d -> errors           = 0;
        d -> header           = (int *) V32DEV_HEADER;
        d -> host             = (int *) (V32DEV_HOST_AREA + offset);
        d -> cart             = (int *) (V32DEV_CART_AREA + offset);

        //////////////////////////////////////////////////////////////////////
        //
        // Take the heartbeats seen right now as the starting point
        //
        if (card_is_connected ())
        {
            d -> emu_beat     = d -> header[V32DEV_HDR_EMU_BEAT];
            d -> host_beat    = d -> host[V32DEV_H_BEAT];
        }
    }

    return (d);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_free(): close the channel and release the instance
//
void    v32dev_free (v32dev **dev)
{
    v32dev *d                 = *dev;

    if (d                    != NULL)
    {
        if (d -> state       == V32DEV_READY)
        {
            d -> cart[V32DEV_C_FLAGS] = 0;
        }
        free (d);
        *dev                  = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_probe(): look at the channel for this frame. Call it once per
// frame (further calls within the same frame do nothing, since the host
// only changes what the program sees between frames). Returns true when
// the state changed, or something happened (see v32dev_events).
//
bool    v32dev_probe (v32dev **dev)
{
    bool    result            = false;
    int     before            = 0;
    int     frame             = 0;
    v32dev *d                 = *dev;

    if (d                    != NULL)
    {
        frame                 = get_frame_counter ();
        if (frame            != d -> frame)
        {
            d -> frame        = frame;
            before            = d -> state;
            v32dev_refresh (d);
            result            = (d -> state != before || d -> events != 0);
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_wait(): end frames, probing each time, until the channel is ready.
// Gives up after the given number of frames (returns false). A mailbox
// needs about 3 frames to be recognized, and a host may take longer.
//
bool    v32dev_wait (v32dev **dev, int frames)
{
    bool    result            = false;

    v32dev_probe (dev);
    while (*dev != NULL && !result)
    {
        if ((*dev) -> state  == V32DEV_READY)
        {
            result            = true;
        }
        else if (frames      <= 0)
        {
            break;
        }
        else
        {
            frames            = frames - 1;
            end_frame ();
            v32dev_probe (dev);
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// State queries (about the latest probe)
//
int     v32dev_state (v32dev **dev)
{
    if (*dev                 != NULL)
    {
        return ((*dev) -> state);
    }
    return (V32DEV_NOCARD);
}

bool    v32dev_ready (v32dev **dev)
{
    return (v32dev_state (dev) == V32DEV_READY);
}

int     v32dev_events (v32dev **dev)
{
    if (*dev                 != NULL)
    {
        return ((*dev) -> events);
    }
    return (0);
}

int     v32dev_type (v32dev **dev)
{
    if (v32dev_ready (dev))
    {
        return ((*dev) -> host[V32DEV_H_TYPE]);
    }
    return (0);
}

int     v32dev_class (v32dev **dev)
{
    return ((v32dev_type (dev) >> 24) & 255);
}

int     v32dev_kind (v32dev **dev)
{
    return (v32dev_type (dev) & 0x00FFFFFF);
}

int     v32dev_status (v32dev **dev)
{
    if (v32dev_ready (dev))
    {
        return ((*dev) -> host[V32DEV_H_STATUS]);
    }
    return (0);
}

//////////////////////////////////////////////////////////////////////////////
//
// Descriptor words: 8 words that the host fills in to describe the device
// (for example, the size of a disk), and 8 that the program fills in to
// make requests of it. What they mean depends on the class.
//
int     v32dev_desc (v32dev **dev, int index)
{
    if (v32dev_ready (dev)
     && index                >= 0
     && index                <  8)
    {
        return ((*dev) -> host[V32DEV_H_DESC + index]);
    }
    return (0);
}

void    v32dev_setdesc (v32dev **dev, int index, int value)
{
    if (v32dev_ready (dev)
     && index                >= 0
     && index                <  8)
    {
        (*dev) -> cart[V32DEV_C_DESC + index] = value;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_avail(): how many words can be read from the host right now.
// A ring index that is out of range means the ring is corrupt: it is
// emptied and counted in errors.
//
int     v32dev_avail (v32dev **dev)
{
    int     head              = 0;
    int     tail              = 0;
    v32dev *d                 = *dev;

    if (d == NULL || d -> state != V32DEV_READY)
    {
        return (0);
    }

    head                      = d -> host[V32DEV_H_H2C_HEAD];
    tail                      = d -> cart[V32DEV_C_H2C_TAIL];
    if (head                 <  0
     || head                 >= V32DEV_RING_WORDS
     || tail                 <  0
     || tail                 >= V32DEV_RING_WORDS)
    {
        d -> errors           = d -> errors + 1;
        d -> cart[V32DEV_C_H2C_TAIL] = 0;
        if (head             >= 0 && head < V32DEV_RING_WORDS)
          d -> cart[V32DEV_C_H2C_TAIL] = head;
        return (0);
    }

    head                      = head - tail;
    if (head                 <  0)
    {
        head                  = head + V32DEV_RING_WORDS;
    }
    return (head);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_space(): how many words can be written to the host right now
//
int     v32dev_space (v32dev **dev)
{
    int     head              = 0;
    int     tail              = 0;
    v32dev *d                 = *dev;

    if (d == NULL || d -> state != V32DEV_READY)
    {
        return (0);
    }

    head                      = d -> cart[V32DEV_C_C2H_HEAD];
    tail                      = d -> host[V32DEV_H_C2H_TAIL];
    if (head                 <  0
     || head                 >= V32DEV_RING_WORDS
     || tail                 <  0
     || tail                 >= V32DEV_RING_WORDS)
    {
        d -> errors           = d -> errors + 1;
        d -> cart[V32DEV_C_C2H_HEAD] = 0;
        if (tail             >= 0 && tail < V32DEV_RING_WORDS)
          d -> cart[V32DEV_C_C2H_HEAD] = tail;
        return (0);
    }

    head                      = tail - head - 1;
    if (head                 <  0)
    {
        head                  = head + V32DEV_RING_WORDS;
    }
    return (head);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_read(): take up to count words from the host (a stream: it may
// return fewer than asked for, or 0). Returns how many were read.
//
int     v32dev_read (v32dev **dev, int *buffer, int count)
{
    int     avail             = v32dev_avail (dev);
    int     tail              = 0;
    v32dev *d                 = *dev;

    if (count                >  avail)
    {
        count                 = avail;
    }
    if (count                <= 0)
    {
        return (0);
    }

    tail                      = d -> cart[V32DEV_C_H2C_TAIL];
    v32dev_ring_get (d -> host + V32DEV_CONTROL_WORDS, tail, buffer, count);
    tail                      = tail + count;
    if (tail                 >= V32DEV_RING_WORDS)
    {
        tail                  = tail - V32DEV_RING_WORDS;
    }
    d -> cart[V32DEV_C_H2C_TAIL] = tail;

    return (count);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_write(): give up to count words to the host (a stream: it may
// take fewer than offered, or 0). Returns how many were taken.
//
int     v32dev_write (v32dev **dev, int *buffer, int count)
{
    int     space             = v32dev_space (dev);
    int     head              = 0;
    v32dev *d                 = *dev;

    if (count                >  space)
    {
        count                 = space;
    }
    if (count                <= 0)
    {
        return (0);
    }

    head                      = d -> cart[V32DEV_C_C2H_HEAD];
    v32dev_ring_put (d -> cart + V32DEV_CONTROL_WORDS, head, buffer, count);
    head                      = head + count;
    if (head                 >= V32DEV_RING_WORDS)
    {
        head                  = head - V32DEV_RING_WORDS;
    }
    d -> cart[V32DEV_C_C2H_HEAD] = head;

    return (count);
}

//////////////////////////////////////////////////////////////////////////////
//
// Messages are carried by the same rings:
//
//     word 0         N         payload size (0 to V32DEV_MSG_MAX)
//     word 1         opcode    defined by the class of device
//     words 2 ...    payload   N words
//
// A message is published all at once, so a receiver that sees any of it
// sees all of it. Streams and messages are not to be mixed in a channel.
//
// v32dev_send(): send a message (returns false, sending nothing, if there
// is no room for all of it). v32dev_sendv() does the same, with a payload
// made by two pieces one after the other (for example a request header
// and the data it carries).
//
bool    v32dev_sendv (v32dev **dev, int opcode, int *first, int firstcount,
                      int *second, int secondcount)
{
    int     total             = firstcount + secondcount;
    int     head              = 0;
    int [2] info;
    v32dev *d                 = *dev;

    if (total                <  0
     || total                >  V32DEV_MSG_MAX
     || firstcount           <  0
     || secondcount          <  0
     || v32dev_space (dev)   <  total + 2)
    {
        return (false);
    }

    info[0]                   = total;
    info[1]                   = opcode;
    head                      = d -> cart[V32DEV_C_C2H_HEAD];
    v32dev_ring_put (d -> cart + V32DEV_CONTROL_WORDS, head, &info[0], 2);
    head                      = head + 2;
    if (head                 >= V32DEV_RING_WORDS)
    {
        head                  = head - V32DEV_RING_WORDS;
    }
    if (firstcount           >  0)
    {
        v32dev_ring_put (d -> cart + V32DEV_CONTROL_WORDS, head, first, firstcount);
        head                  = head + firstcount;
        if (head             >= V32DEV_RING_WORDS)
        {
            head              = head - V32DEV_RING_WORDS;
        }
    }
    if (secondcount          >  0)
    {
        v32dev_ring_put (d -> cart + V32DEV_CONTROL_WORDS, head, second, secondcount);
        head                  = head + secondcount;
        if (head             >= V32DEV_RING_WORDS)
        {
            head              = head - V32DEV_RING_WORDS;
        }
    }

    d -> cart[V32DEV_C_C2H_HEAD] = head;
    return (true);
}

bool    v32dev_send (v32dev **dev, int opcode, int *payload, int count)
{
    return (v32dev_sendv (dev, opcode, payload, count, NULL, 0));
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_next(): payload size of the next message, or -1 if none has
// arrived. If the ring does not hold a well formed message it is
// emptied (and counted in errors), and -1 is returned.
//
int     v32dev_next (v32dev **dev)
{
    int     avail             = v32dev_avail (dev);
    int     tail              = 0;
    int     size              = 0;
    int [2] info;
    v32dev *d                 = *dev;

    if (avail                <  2)
    {
        return (-1);
    }

    tail                      = d -> cart[V32DEV_C_H2C_TAIL];
    v32dev_ring_get (d -> host + V32DEV_CONTROL_WORDS, tail, &info[0], 2);
    size                      = info[0];
    if (size                 <  0
     || size                 >  V32DEV_MSG_MAX
     || size + 2             >  avail)
    {
        d -> errors           = d -> errors + 1;
        tail                  = tail + avail;
        if (tail             >= V32DEV_RING_WORDS)
        {
            tail              = tail - V32DEV_RING_WORDS;
        }
        d -> cart[V32DEV_C_H2C_TAIL] = tail;
        return (-1);
    }

    return (size);
}

//////////////////////////////////////////////////////////////////////////////
//
// v32dev_recv(): take the next message, if there is one. Returns its
// payload size, or -1 if there was no message. A message with more payload
// than max is discarded, and -2 is returned.
//
int     v32dev_recv (v32dev **dev, int *opcode, int *payload, int max)
{
    int     size              = v32dev_next (dev);
    int     tail              = 0;
    int [2] info;
    v32dev *d                 = *dev;

    if (size                 <  0)
    {
        return (-1);
    }

    tail                      = d -> cart[V32DEV_C_H2C_TAIL];
    v32dev_ring_get (d -> host + V32DEV_CONTROL_WORDS, tail, &info[0], 2);
    *opcode                   = info[1];
    tail                      = tail + 2;
    if (tail                 >= V32DEV_RING_WORDS)
    {
        tail                  = tail - V32DEV_RING_WORDS;
    }

    if (size                 >  max)
    {
        size                  = -2;
    }
    else if (size            >  0)
    {
        v32dev_ring_get (d -> host + V32DEV_CONTROL_WORDS, tail, payload, size);
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Either way the message is consumed
    //
    if (size                 >  0)
    {
        tail                  = tail + size;
    }
    else if (size            == -2)
    {
        tail                  = tail + info[0];
    }
    if (tail                 >= V32DEV_RING_WORDS)
    {
        tail                  = tail - V32DEV_RING_WORDS;
    }
    d -> cart[V32DEV_C_H2C_TAIL] = tail;

    return (size);
}

#endif
