// Log.h for the Mac/PC tools: lines go to stderr.
#include <stdio.h>
#include <stdarg.h>
#include "Log.h"

void Log::Open( const char* ) {}
void Log::SetRemote( const char*, int ) {}

void Log::Write( const char* fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    fprintf( stderr, "  [log] " );
    vfprintf( stderr, fmt, args );
    fprintf( stderr, "\n" );
    va_end( args );
}
