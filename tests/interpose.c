// LD_PRELOAD shim for the write-order test: logs "<file offset> <length>" for
// every write(2) to a *.memc file into the file named by $DEV_WRITE_LOG.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ssize_t (*real_write)( int, const void*, size_t );

ssize_t write( int fd, const void* buf, size_t n )
{
    if( !real_write )
      real_write = (ssize_t (*)( int, const void*, size_t ))dlsym( RTLD_NEXT, "write" );

    const char* LogPath = getenv( "DEV_WRITE_LOG" );

    if( LogPath && n > 0 )
    {
        char Link[64], Target[512];
        snprintf( Link, sizeof( Link ), "/proc/self/fd/%d", fd );
        ssize_t Len = readlink( Link, Target, sizeof( Target ) - 1 );

        if( Len > 0 )
        {
            Target[ Len ] = 0;
            off_t Pos = lseek( fd, 0, SEEK_CUR );

            if( strstr( Target, ".memc" ) && Pos >= 0 )
            {
                char Line[96];
                int LineLen = snprintf( Line, sizeof( Line ), "%ld %zu\n", (long)Pos, n );
                int LogFd = open( LogPath, O_WRONLY | O_APPEND | O_CREAT, 0644 );
                if( LogFd >= 0 )
                {
                    real_write( LogFd, Line, LineLen );
                    close( LogFd );
                }
            }
        }
    }

    return real_write( fd, buf, n );
}
