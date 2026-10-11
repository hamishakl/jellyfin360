#pragma once
// Lets the server-facing code (Json, JellyfinApi, JellyfinAuth) build both for the console and
// natively on a PC/Mac for tools\mac (jftool), where it talks to a real Jellyfin through libcurl.
#ifdef _XBOX
#include <xtl.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef long long __int64;
// Only the array forms are used in portable code: sprintf_s( char buf[N], fmt, ... ).
#define sprintf_s( buf, ... )  snprintf( buf, sizeof( buf ), __VA_ARGS__ )
#define swprintf_s( buf, ... ) swprintf( buf, sizeof( buf ) / sizeof( ( buf )[0] ), __VA_ARGS__ )
#endif
