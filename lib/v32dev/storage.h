#ifndef _STORAGE_H
#define _STORAGE_H

//////////////////////////////////////////////////////////////////////////////
//
// v32tools/v32dev library: "storage.h"          File version: 2026/10/15
// ----------------------------------------------------------------------
//
// This file contains all definitions and functions needed by programs to
// use a STORAGE device: a block device (class V32DEV_CLASS_BLOCK, kind
// V32DEV_KIND_STORAGE) served by the host. Typically it is a file on the
// host computer (a disk image) that the program can read and write in
// sectors, which is the way to get data in and out of the emulator.
//
// Reading and writing the mailbox itself is done by the v32dev core
// (v32dev.h); this library sends it requests and interprets the replies.
//
// The device is made of SECTORS of a fixed size, announced by the host
// (storage_sector_words), and numbered from 0. Each word of the program
// is one word of the device, with one exception: when the host serves
// the file in BYTE mode (storage_bytes), each word of the device holds
// one byte, 0 to 255 (a write keeps only the lowest 8 bits). That way a
// host file such as a text file or a FAT image can be used directly, one
// character per word. Otherwise each word is stored as 4 bytes
// (little endian) on the host.
//
// There are two ways to use it:
//
//   - blocking: storage_read(), storage_write() and storage_flush() wait
//     (ending frames) until the request is done. Easy, but the program
//     does nothing else meanwhile. A request takes at least 1 frame, and
//     more if the host is slow to answer.
//
//   - not blocking: storage_read_begin() and its siblings start a request
//     and return; call storage_probe() every frame, and storage_busy()
//     tells when it is done (the result is in storage_result()).
//
// Only one request is in progress at a time. Each request moves at most
// storage_max_sectors() sectors; the blocking functions split bigger ones.
//
// The program can also address the device by word (storage_read_words and
// storage_write_words), to move any number of words from any position.
//
// IMPORTANT: storage_probe() must be called once on EVERY frame (the
// blocking functions do it themselves).
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//
// include the v32dev core (it includes the needed DevTools headers)
//
#include "v32dev.h"

//////////////////////////////////////////////////////////////////////////////
//
// meaning of the descriptor words that the host announces
//
#define STORAGE_DESC_SECTOR_WORDS       0  // words in a sector
#define STORAGE_DESC_SECTORS            1  // sectors in the device
#define STORAGE_DESC_FLAGS              2  // STORAGE_FLAG_ bits
#define STORAGE_DESC_MAX_SECTORS        3  // most sectors per request

#define STORAGE_FLAG_READONLY           1  // device cannot be written
#define STORAGE_FLAG_BYTES              2  // each word holds one byte

//////////////////////////////////////////////////////////////////////////////
//
// requests (program to host) and replies (host to program)
//
//   STORAGE_OP_READ    [tag, sector, count]           -> REPLY_READ
//   STORAGE_OP_WRITE   [tag, sector, count, words...] -> REPLY_WRITE
//   STORAGE_OP_FLUSH   [tag]                          -> REPLY_FLUSH
//
//   STORAGE_REPLY_READ   [tag, status, words...]
//   STORAGE_REPLY_WRITE  [tag, status]
//   STORAGE_REPLY_FLUSH  [tag, status]
//
#define STORAGE_OP_READ                 1
#define STORAGE_OP_WRITE                2
#define STORAGE_OP_FLUSH                3
#define STORAGE_REPLY_READ            129
#define STORAGE_REPLY_WRITE           130
#define STORAGE_REPLY_FLUSH           131

//////////////////////////////////////////////////////////////////////////////
//
// results
//
#define STORAGE_OK                      0  // done
#define STORAGE_EINVAL                  1  // bad request
#define STORAGE_ERANGE                  2  // sectors outside the device
#define STORAGE_EROFS                   3  // device is read only
#define STORAGE_EIO                     4  // host could not do it
#define STORAGE_ETIMEOUT                5  // host did not answer in time
#define STORAGE_ENODEV                  6  // device not usable (now)
#define STORAGE_EBUSY                   7  // another request is in progress
#define STORAGE_ENOMEM                  8  // out of memory
#define STORAGE_EABORT                  9  // request lost: host restarted

//////////////////////////////////////////////////////////////////////////////
//
// library parameters
//
#define STORAGE_TIMEOUT               300  // frames to wait for a reply

//////////////////////////////////////////////////////////////////////////////
//
// storage is a storage device on a channel
//
struct storage
{
    v32dev *dev;             // the channel
    bool    ready;           // geometry below is valid
    int     sector_words;    // words in a sector
    int     sectors;         // sectors in the device
    int     flags;           // STORAGE_FLAG_ bits
    int     max_sectors;     // most sectors per request
    bool    busy;            // a request is in progress
    int     op;              // STORAGE_OP_ of the request in progress
    int     tag;             // identifies the request in progress
    int     status;          // result of the latest request
    int     count;           // sectors in the request in progress
    int    *buffer;          // where a read request puts its data
    int     deadline;        // frame at which the request times out
    int    *reply;           // receives replies (reply header + a request's data)
    int    *sector;          // one sector, for requests by word
};

//////////////////////////////////////////////////////////////////////////////
//
// API: function prototypes for the functions in this library
//
storage *storage_init         (int channel);                     // new instance
void     storage_free         (storage **st);                    // release it
bool     storage_probe        (storage **st);                    // once per frame
bool     storage_wait_ready   (storage **st, int frames);        // wait for host
bool     storage_ready        (storage **st);                    // usable now?
int      storage_sector_words (storage **st);                    // sector size
int      storage_sectors      (storage **st);                    // device size
int      storage_max_sectors  (storage **st);                    // per request
bool     storage_readonly     (storage **st);                    // cannot write?
bool     storage_bytes        (storage **st);                    // byte mode?
bool     storage_busy         (storage **st);                    // request pending?
int      storage_result       (storage **st);                    // latest result
int      storage_read_begin   (storage **st, int sector, int count, int *dest);
int      storage_write_begin  (storage **st, int sector, int count, int *source);
int      storage_flush_begin  (storage **st);
int      storage_wait         (storage **st);                    // finish request
int      storage_read         (storage **st, int sector, int count, int *dest);
int      storage_write        (storage **st, int sector, int count, int *source);
int      storage_flush        (storage **st);
int      storage_read_words   (storage **st, int offset, int *dest, int count);
int      storage_write_words  (storage **st, int offset, int *source, int count);

//////////////////////////////////////////////////////////////////////////////
//
// internal: end the request in progress with a result
//
void     storage_finish (storage *s, int status)
{
    s -> busy                 = false;
    s -> status               = status;
}

//////////////////////////////////////////////////////////////////////////////
//
// internal: learn the geometry from the descriptor words of the host
//
void     storage_describe (storage *s)
{
    int     size              = 0;

    s -> sector_words         = v32dev_desc (&(s -> dev), STORAGE_DESC_SECTOR_WORDS);
    s -> sectors              = v32dev_desc (&(s -> dev), STORAGE_DESC_SECTORS);
    s -> flags                = v32dev_desc (&(s -> dev), STORAGE_DESC_FLAGS);
    s -> max_sectors          = v32dev_desc (&(s -> dev), STORAGE_DESC_MAX_SECTORS);

    //////////////////////////////////////////////////////////////////////////
    //
    // Check that what the host says makes sense, and that a request fits
    // in the rings (a request or reply has 3 or 4 words besides the data)
    //
    if (s -> sector_words    <= 0
     || s -> sectors         <  0
     || s -> max_sectors     <= 0
     || s -> sector_words > V32DEV_MSG_MAX - 4)
    {
        s -> ready            = false;
        return;
    }
    if (s -> max_sectors * s -> sector_words > V32DEV_MSG_MAX - 4)
    {
        s -> max_sectors      = (V32DEV_MSG_MAX - 4) / s -> sector_words;
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // The buffers depend on the geometry, so they are made again when it
    // changes
    //
    size                      = (s -> max_sectors * s -> sector_words) + 4;
    if (s -> reply           != NULL)
    {
        free (s -> reply);
    }
    if (s -> sector          != NULL)
    {
        free (s -> sector);
    }
    s -> reply                = (int *) malloc (size);
    s -> sector               = (int *) malloc (s -> sector_words);
    s -> ready                = (s -> reply != NULL && s -> sector != NULL);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_init(): initialize a new storage device on a channel (0 to 3).
// It is usable after a probe says so (see storage_wait_ready).
//
storage *storage_init (int channel)
{
    storage *s                = NULL;

    s                         = (storage *) malloc (sizeof (storage) * 1);
    if (s                    != NULL)
    {
        s -> dev              = v32dev_init (channel, V32DEV_CLASS_BLOCK);
        s -> ready            = false;
        s -> sector_words     = 0;
        s -> sectors          = 0;
        s -> flags            = 0;
        s -> max_sectors      = 0;
        s -> busy             = false;
        s -> op               = 0;
        s -> tag              = 0;
        s -> status           = STORAGE_OK;
        s -> count            = 0;
        s -> buffer           = NULL;
        s -> deadline         = 0;
        s -> reply            = NULL;
        s -> sector           = NULL;

        if (s -> dev         == NULL)
        {
            free (s);
            s                 = NULL;
        }
    }

    return (s);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_free(): release a storage instance
//
void    storage_free (storage **st)
{
    storage *s                = *st;

    if (s                    != NULL)
    {
        v32dev_free (&(s -> dev));
        if (s -> reply       != NULL)
        {
            free (s -> reply);
        }
        if (s -> sector      != NULL)
        {
            free (s -> sector);
        }
        free (s);
        *st                   = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_probe(): look at the device for this frame; call it once per
// frame. It takes the replies to the request in progress, and ends the
// request if it timed out or the host went away. Returns true when a
// request ended (or the device became or stopped being ready).
//
bool    storage_probe (storage **st)
{
    bool    result            = false;
    bool    before            = false;
    int     events            = 0;
    int     size              = 0;
    int     opcode            = 0;
    int     frame             = 0;
    storage *s                = *st;

    if (s                    == NULL)
    {
        return (false);
    }

    before                    = s -> ready;
    v32dev_probe (&(s -> dev));
    events                    = v32dev_events (&(s -> dev));

    //////////////////////////////////////////////////////////////////////////
    //
    // The device is usable while the channel is ready; the host may
    // have been replaced by another with different geometry, so it is
    // read again each time the channel opens or the host restarts
    //
    if ((events & (V32DEV_EV_OPEN | V32DEV_EV_RESET)) != 0)
    {
        storage_describe (s);
        if (s -> busy)
        {
            storage_finish (s, STORAGE_EABORT);
            result            = true;
        }
    }
    if (!v32dev_ready (&(s -> dev)))
    {
        s -> ready            = false;
        if (s -> busy)
        {
            storage_finish (s, STORAGE_ENODEV);
            result            = true;
        }
    }
    if (s -> ready != before)
    {
        result                = true;
    }

    //////////////////////////////////////////////////////////////////////////
    //
    // Take the replies that have arrived. Anything that does not belong
    // to the request in progress (a late reply to an earlier one) is
    // ignored.
    //
    if (s -> ready)
    {
        size                  = v32dev_recv (&(s -> dev), &opcode, s -> reply,
                                             (s -> max_sectors * s -> sector_words) + 4);
        while (size          >= 0)
        {
            if (s -> busy
             && size         >= 2
             && s -> reply[0] == s -> tag
             && opcode       == (s -> op + 128))
            {
                if (s -> op  == STORAGE_OP_READ
                 && s -> reply[1] == STORAGE_OK)
                {
                    if (size == 2 + (s -> count * s -> sector_words))
                    {
                        memcpy (s -> buffer, s -> reply + 2, s -> count * s -> sector_words);
                        storage_finish (s, STORAGE_OK);
                    }
                    else
                    {
                        storage_finish (s, STORAGE_EIO);
                    }
                }
                else
                {
                    storage_finish (s, s -> reply[1]);
                }
                result        = true;
            }
            size              = v32dev_recv (&(s -> dev), &opcode, s -> reply,
                                             (s -> max_sectors * s -> sector_words) + 4);
        }

        frame                 = get_frame_counter ();
        if (s -> busy && frame - s -> deadline >= 0)
        {
            storage_finish (s, STORAGE_ETIMEOUT);
            result            = true;
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_wait_ready(): end frames, probing each time, until the device is
// usable. Gives up after the given number of frames (returns false).
//
bool    storage_wait_ready (storage **st, int frames)
{
    storage_probe (st);
    while (*st               != NULL && !(*st) -> ready && frames > 0)
    {
        frames                = frames - 1;
        end_frame ();
        storage_probe (st);
    }

    return (*st != NULL && (*st) -> ready);
}

//////////////////////////////////////////////////////////////////////////////
//
// Queries (as of the latest probe)
//
bool    storage_ready (storage **st)
{
    return (*st != NULL && (*st) -> ready);
}

int     storage_sector_words (storage **st)
{
    if (*st                  != NULL)
    {
        return ((*st) -> sector_words);
    }
    return (0);
}

int     storage_sectors (storage **st)
{
    if (*st                  != NULL)
    {
        return ((*st) -> sectors);
    }
    return (0);
}

int     storage_max_sectors (storage **st)
{
    if (*st                  != NULL)
    {
        return ((*st) -> max_sectors);
    }
    return (0);
}

bool    storage_readonly (storage **st)
{
    return (*st != NULL && ((*st) -> flags & STORAGE_FLAG_READONLY) != 0);
}

bool    storage_bytes (storage **st)
{
    return (*st != NULL && ((*st) -> flags & STORAGE_FLAG_BYTES) != 0);
}

bool    storage_busy (storage **st)
{
    return (*st != NULL && (*st) -> busy);
}

int     storage_result (storage **st)
{
    if (*st                  != NULL)
    {
        return ((*st) -> status);
    }
    return (STORAGE_ENODEV);
}

//////////////////////////////////////////////////////////////////////////////
//
// internal: check a request and start it. count is the sectors involved.
//
int     storage_start (storage *s, int op, int sector, int count)
{
    if (s -> busy)
    {
        return (STORAGE_EBUSY);
    }
    if (!s -> ready)
    {
        return (STORAGE_ENODEV);
    }
    if (op                   != STORAGE_OP_FLUSH
     && (count               <= 0
      || count               >  s -> max_sectors
      || sector              <  0
      || sector + count      >  s -> sectors
      || sector + count      <  0))
    {
        return (STORAGE_ERANGE);
    }
    if (op                   == STORAGE_OP_WRITE
     && (s -> flags & STORAGE_FLAG_READONLY) != 0)
    {
        return (STORAGE_EROFS);
    }

    s -> tag                  = s -> tag + 1;
    s -> op                   = op;
    s -> count                = count;
    s -> deadline             = get_frame_counter () + STORAGE_TIMEOUT;
    return (STORAGE_OK);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_read_begin(): start reading count sectors, from the given one,
// into dest (it must have room for count * storage_sector_words words and
// must stay valid until the request is done). Returns STORAGE_OK when it
// has started, or the reason it did not.
//
int     storage_read_begin (storage **st, int sector, int count, int *dest)
{
    int     result            = STORAGE_OK;
    int [3] request;
    storage *s                = *st;

    if (s                    == NULL)
    {
        return (STORAGE_ENODEV);
    }

    result                    = storage_start (s, STORAGE_OP_READ, sector, count);
    if (result               == STORAGE_OK)
    {
        request[0]            = s -> tag;
        request[1]            = sector;
        request[2]            = count;
        s -> buffer           = dest;
        if (v32dev_send (&(s -> dev), STORAGE_OP_READ, &request[0], 3))
        {
            s -> busy         = true;
        }
        else
        {
            result            = STORAGE_EIO;
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_write_begin(): start writing count sectors, from the given one,
// with the words in source. The words are copied right away, so source can
// be reused as soon as this returns.
//
int     storage_write_begin (storage **st, int sector, int count, int *source)
{
    int     result            = STORAGE_OK;
    int [3] request;
    storage *s                = *st;

    if (s                    == NULL)
    {
        return (STORAGE_ENODEV);
    }

    result                    = storage_start (s, STORAGE_OP_WRITE, sector, count);
    if (result               == STORAGE_OK)
    {
        request[0]            = s -> tag;
        request[1]            = sector;
        request[2]            = count;
        if (v32dev_sendv (&(s -> dev), STORAGE_OP_WRITE, &request[0], 3,
                          source, count * s -> sector_words))
        {
            s -> busy         = true;
        }
        else
        {
            result            = STORAGE_EIO;
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_flush_begin(): start asking the host to make sure that everything
// written is on its storage
//
int     storage_flush_begin (storage **st)
{
    int     result            = STORAGE_OK;
    int [1] request;
    storage *s                = *st;

    if (s                    == NULL)
    {
        return (STORAGE_ENODEV);
    }

    result                    = storage_start (s, STORAGE_OP_FLUSH, 0, 0);
    if (result               == STORAGE_OK)
    {
        request[0]            = s -> tag;
        if (v32dev_send (&(s -> dev), STORAGE_OP_FLUSH, &request[0], 1))
        {
            s -> busy         = true;
        }
        else
        {
            result            = STORAGE_EIO;
        }
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_wait(): end frames until the request in progress is done (or has
// failed, or timed out). Returns its result.
//
int     storage_wait (storage **st)
{
    while (*st               != NULL && (*st) -> busy)
    {
        end_frame ();
        storage_probe (st);
    }

    return (storage_result (st));
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_read(), storage_write(), storage_flush(): do the whole thing
// and return the result (STORAGE_OK, or what failed). Requests bigger
// than storage_max_sectors() are split. The device must be ready.
//
int     storage_read (storage **st, int sector, int count, int *dest)
{
    int     result            = STORAGE_OK;
    int     chunk             = 0;

    if (*st                  == NULL)
    {
        return (STORAGE_ENODEV);
    }

    while (count             >  0 && result == STORAGE_OK)
    {
        chunk                 = count;
        if (chunk            >  (*st) -> max_sectors)
        {
            chunk             = (*st) -> max_sectors;
        }
        result                = storage_read_begin (st, sector, chunk, dest);
        if (result           == STORAGE_OK)
        {
            result            = storage_wait (st);
        }
        sector                = sector + chunk;
        count                 = count - chunk;
        dest                  = dest + (chunk * (*st) -> sector_words);
    }

    return (result);
}

int     storage_write (storage **st, int sector, int count, int *source)
{
    int     result            = STORAGE_OK;
    int     chunk             = 0;

    if (*st                  == NULL)
    {
        return (STORAGE_ENODEV);
    }

    while (count             >  0 && result == STORAGE_OK)
    {
        chunk                 = count;
        if (chunk            >  (*st) -> max_sectors)
        {
            chunk             = (*st) -> max_sectors;
        }
        result                = storage_write_begin (st, sector, chunk, source);
        if (result           == STORAGE_OK)
        {
            result            = storage_wait (st);
        }
        sector                = sector + chunk;
        count                 = count - chunk;
        source                = source + (chunk * (*st) -> sector_words);
    }

    return (result);
}

int     storage_flush (storage **st)
{
    int     result            = storage_flush_begin (st);

    if (result               == STORAGE_OK)
    {
        result                = storage_wait (st);
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_read_words(): read count words starting at word number offset of
// the device (any position, any amount). Sectors that are read entirely go
// straight to dest; the ends are read through a sector buffer.
//
int     storage_read_words (storage **st, int offset, int *dest, int count)
{
    int     result            = STORAGE_OK;
    int     size              = 0;
    int     sector            = 0;
    int     inside            = 0;
    int     part              = 0;
    int     whole             = 0;
    storage *s                = *st;

    if (s == NULL || !s -> ready)
    {
        return (STORAGE_ENODEV);
    }

    size                      = s -> sector_words;
    if (offset               <  0
     || count                <  0
     || offset + count       <  offset
     || (count               >  0
      && (offset + count - 1) / size >= s -> sectors))
    {
        return (STORAGE_ERANGE);
    }

    while (count             >  0 && result == STORAGE_OK)
    {
        sector                = offset / size;
        inside                = offset % size;
        whole                 = count / size;

        if (inside           == 0 && whole > 0)
        {
            //////////////////////////////////////////////////////////////////
            //
            // Whole sectors, directly
            //
            result            = storage_read (st, sector, whole, dest);
            part              = whole * size;
        }
        else
        {
            //////////////////////////////////////////////////////////////////
            //
            // Part of a sector, through the buffer
            //
            result            = storage_read (st, sector, 1, s -> sector);
            part              = size - inside;
            if (part         >  count)
            {
                part          = count;
            }
            if (result       == STORAGE_OK)
            {
                memcpy (dest, s -> sector + inside, part);
            }
        }
        offset                = offset + part;
        dest                  = dest + part;
        count                 = count - part;
    }

    return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
// storage_write_words(): write count words starting at word number offset
// of the device. The sectors that are only partly written are read first,
// to keep the rest of their words.
//
int     storage_write_words (storage **st, int offset, int *source, int count)
{
    int     result            = STORAGE_OK;
    int     size              = 0;
    int     sector            = 0;
    int     inside            = 0;
    int     part              = 0;
    int     whole             = 0;
    storage *s                = *st;

    if (s == NULL || !s -> ready)
    {
        return (STORAGE_ENODEV);
    }

    size                      = s -> sector_words;
    if (offset               <  0
     || count                <  0
     || offset + count       <  offset
     || (count               >  0
      && (offset + count - 1) / size >= s -> sectors))
    {
        return (STORAGE_ERANGE);
    }

    while (count             >  0 && result == STORAGE_OK)
    {
        sector                = offset / size;
        inside                = offset % size;
        whole                 = count / size;

        if (inside           == 0 && whole > 0)
        {
            result            = storage_write (st, sector, whole, source);
            part              = whole * size;
        }
        else
        {
            result            = storage_read (st, sector, 1, s -> sector);
            part              = size - inside;
            if (part         >  count)
            {
                part          = count;
            }
            if (result       == STORAGE_OK)
            {
                memcpy (s -> sector + inside, source, part);
                result        = storage_write (st, sector, 1, s -> sector);
            }
        }
        offset                = offset + part;
        source                = source + part;
        count                 = count - part;
    }

    return (result);
}

#endif
