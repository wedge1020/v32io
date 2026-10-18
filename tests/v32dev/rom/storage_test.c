#include "video.h"
#include "storage.h"

//////////////////////////////////////////////////////////////////////////////
//
// storage.h test program, run by dev_rom_test.py in the (patched) emulator
// code against a host. The scenario to run is read from word 64 of the card,
// and a report is written to words 65 to 128 of the card (where the test
// reads it): it only has what the program itself can observe.
//
// pattern of the images the host makes: word i = (i << 17) ^ (i * 3) ^ 0x80000001
//
#define SCENARIO_WORD    64
#define REPORT_WORD      65
#define REPORT_SIZE      48

int pattern (int i)
{
    return ((i << 17) ^ (i * 3) ^ 0x80000001);
}

int [REPORT_SIZE] rep;
int [2048] buffer;
int [2048] other;

void report (int stage)
{
    rep[1] = stage;
    card_write_data (rep, REPORT_WORD, REPORT_SIZE);
}

// read count words from the device and compare with the pattern; returns mismatches
int check_pattern (storage **st, int offset, int count)
{
    int     i                 = 0;
    int     bad               = 0;
    int     result            = storage_read_words (st, offset, buffer, count);

    if (result != STORAGE_OK)
    {
        return (100000 + result);
    }
    for (i = 0; i < count; i = i + 1)
    {
        if (buffer[i] != pattern (offset + i))
        {
            bad               = bad + 1;
        }
    }
    return (bad);
}

//////////////////////////////////////////////////////////////////////////////
//
// scenario 1: read and write a word mode image
//
void scenario_rw (storage **st)
{
    int     i                 = 0;
    int     bad               = 0;
    int     start             = 0;
    int     result            = 0;
    int     sw                = storage_sector_words (st);

    rep[3]                    = storage_sector_words (st);
    rep[4]                    = storage_sectors (st);
    rep[5]                    = storage_readonly (st);
    rep[6]                    = storage_bytes (st);
    rep[7]                    = storage_max_sectors (st);

    // whole sectors
    result                    = storage_read (st, 0, 5, buffer);
    rep[8]                    = result;
    bad                       = 0;
    for (i = 0; i < 5 * sw; i = i + 1)
    {
        if (buffer[i] != pattern (i))
        {
            bad               = bad + 1;
        }
    }
    rep[9]                    = bad;

    // by word, crossing sector boundaries, starting inside a sector
    start                     = get_frame_counter ();
    rep[10]                   = check_pattern (st, 77, 1000);
    rep[11]                   = get_frame_counter () - start;

    // write by word (crosses sectors, partial at both ends), then flush
    for (i = 0; i < 700; i = i + 1)
    {
        other[i]              = 0xC0DE0000 + i;
    }
    rep[12]                   = storage_write_words (st, 2000, other, 700);
    rep[13]                   = storage_flush (st);

    // read back: the written words are there, and their neighbours are intact
    bad                       = 0;
    rep[14]                   = storage_read_words (st, 1990, buffer, 720);
    for (i = 0; i < 720; i = i + 1)
    {
        if (i >= 10 && i < 710)
        {
            if (buffer[i] != 0xC0DE0000 + (i - 10))
            {
                bad           = bad + 1;
            }
        }
        else if (buffer[i] != pattern (1990 + i))
        {
            bad               = bad + 1;
        }
    }
    rep[15]                   = bad;

    // whole sectors, several in one request (split by the library if needed)
    for (i = 0; i < 5 * sw; i = i + 1)
    {
        other[i]              = ~i;
    }
    rep[16]                   = storage_write (st, 40, 5, other);
    rep[17]                   = storage_flush (st);
    rep[18]                   = storage_read (st, 40, 5, buffer);
    bad                       = 0;
    for (i = 0; i < 5 * sw; i = i + 1)
    {
        if (buffer[i] != ~i)
        {
            bad               = bad + 1;
        }
    }
    rep[19]                   = bad;

    // errors
    rep[20]                   = storage_read (st, storage_sectors (st), 1, buffer);
    rep[21]                   = storage_read_words (st, storage_sectors (st) * sw - 5, buffer, 6);
    rep[22]                   = storage_read (st, -1, 1, buffer);
    rep[23]                   = storage_read (st, 0, 0, buffer);

    // not blocking: one request at a time, and how many frames it takes
    start                     = get_frame_counter ();
    rep[24]                   = storage_read_begin (st, 10, 1, buffer);
    rep[25]                   = storage_read_begin (st, 11, 1, other);     // busy
    while (storage_busy (st))
    {
        end_frame ();
        storage_probe (st);
    }
    rep[26]                   = storage_result (st);
    rep[27]                   = get_frame_counter () - start;
    bad                       = 0;
    for (i = 0; i < sw; i = i + 1)
    {
        if (buffer[i] != pattern (10 * sw + i))
        {
            bad               = bad + 1;
        }
    }
    rep[28]                   = bad;

    // last word of the device
    rep[29]                   = check_pattern (st, storage_sectors (st) * sw - 3, 3);
}

//////////////////////////////////////////////////////////////////////////////
//
// scenario 2: byte mode, read only
//
void scenario_bytes (storage **st)
{
    int     i                 = 0;

    rep[3]                    = storage_sector_words (st);
    rep[4]                    = storage_sectors (st);
    rep[5]                    = storage_readonly (st);
    rep[6]                    = storage_bytes (st);
    rep[7]                    = storage_max_sectors (st);

    rep[8]                    = storage_read_words (st, 0, buffer, 32);
    for (i = 0; i < 32; i = i + 1)
    {
        rep[16 + i]           = buffer[i];
    }
    for (i = 0; i < 32; i = i + 1)
    {
        buffer[i]             = 65;
    }
    rep[9]                    = storage_write_words (st, 0, buffer, 32);
    rep[10]                   = storage_write (st, 0, 1, buffer);
    rep[11]                   = storage_write_begin (st, 0, 1, buffer);
}

//////////////////////////////////////////////////////////////////////////////
//
// scenario 7: keep reading while the host crashes and comes back
//
void scenario_survive (storage **st)
{
    int     round             = 0;
    int     good              = 0;
    int     fails             = 0;
    int     after             = 0;
    int     result            = 0;
    int     first             = 0;

    while (round < 8000 && after < 20)
    {
        if (storage_ready (st))
        {
            result            = check_pattern (st, 100, 10);
            if (result       == 0)
            {
                good          = good + 1;
                if (fails    >  0)
                {
                    after     = after + 1;
                }
            }
            else
            {
                if (fails    == 0)
                {
                    first     = 99;                    // wrong data
                    if (result >= 100000)
                    {
                        first = result - 100000;       // a failed request
                    }
                }
                fails         = fails + 1;
            }
        }
        else
        {
            if (good         >  0 && fails == 0)
            {
                fails         = 1;
                first         = 1000;
            }
            end_frame ();
            storage_probe (st);
        }
        round                 = round + 1;
    }
    rep[3]                    = good;
    rep[4]                    = fails;
    rep[5]                    = first;
    rep[6]                    = after;
    rep[7]                    = round;
}

void main (void)
{
    int         scenario      = 0;
    int         i             = 0;
    bool        ready         = false;
    storage    *st            = NULL;

    for (i = 0; i < REPORT_SIZE; i = i + 1)
    {
        rep[i]                = 0;
    }
    rep[0]                    = 0x54504552;                // "REPT"
    rep[1]                    = 0;
    card_read_data (&scenario, SCENARIO_WORD, 1);
    rep[2]                    = scenario;

    st                        = storage_init (1);
    ready                     = storage_wait_ready (&st, 400);
    rep[40]                   = v32dev_state (&(st -> dev));       // channel state
    rep[41]                   = ready;
    rep[42]                   = v32dev_class (&(st -> dev));
    rep[43]                   = v32dev_kind (&(st -> dev));

    if (ready)
    {
        if (scenario         == 1)
        {
            scenario_rw (&st);
        }
        else if (scenario    == 2)
        {
            scenario_bytes (&st);
        }
        else if (scenario    == 7)
        {
            scenario_survive (&st);
        }
    }
    else if (scenario        == 7)
    {
        scenario_survive (&st);
    }

    report (99);
    storage_free (&st);
}
