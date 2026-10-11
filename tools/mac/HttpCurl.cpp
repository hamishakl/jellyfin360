// Http.h for the Mac/PC tools: libcurl instead of the console's XNet + mbedTLS.
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Http.h"

namespace
{
    size_t Collect( char* data, size_t size, size_t n, void* user )
    {
        ( (std::string*)user )->append( data, size * n );
        return size * n;
    }

    curl_slist* HeaderList( const std::string& extraHeaders )
    {
        curl_slist* list = NULL;
        size_t pos = 0;
        while( pos < extraHeaders.size() )
        {
            size_t eol = extraHeaders.find( "\r\n", pos );
            if( eol == std::string::npos )
                eol = extraHeaders.size();
            std::string line = extraHeaders.substr( pos, eol - pos );
            if( !line.empty() )
                list = curl_slist_append( list, line.c_str() );
            pos = eol + 2;
        }
        return list;
    }
}

bool Http::Url::Parse( const std::string& url )
{
    size_t p = url.find( "://" );
    if( p == std::string::npos )
        return false;
    https = url.compare( 0, p, "https" ) == 0;
    port = https ? 443 : 80;
    size_t hostStart = p + 3;
    size_t pathStart = url.find( '/', hostStart );
    std::string hostPort = url.substr( hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart );
    path = pathStart == std::string::npos ? "/" : url.substr( pathStart );
    size_t colon = hostPort.find( ':' );
    host = hostPort.substr( 0, colon );
    if( colon != std::string::npos )
        port = atoi( hostPort.c_str() + colon + 1 );
    return !host.empty();
}

bool Http::Startup( std::string& error, std::string& localIp )
{
    curl_global_init( CURL_GLOBAL_DEFAULT );
    localIp = "127.0.0.1";
    return true;
}

Http::Response Http::Request( const char* method, const std::string& url, const std::string& extraHeaders,
                              const std::string& body, unsigned long timeoutMs )
{
    Response r;
    CURL* c = curl_easy_init();
    if( !c )
    {
        r.error = "curl init failed";
        return r;
    }
    curl_slist* headers = HeaderList( extraHeaders );
    if( body.empty() && ( strcmp( method, "POST" ) == 0 || strcmp( method, "PUT" ) == 0 ) )
        headers = curl_slist_append( headers, "Content-Length: 0" );
    curl_easy_setopt( c, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( c, CURLOPT_CUSTOMREQUEST, method );
    curl_easy_setopt( c, CURLOPT_HTTPHEADER, headers );
    curl_easy_setopt( c, CURLOPT_TIMEOUT_MS, (long)timeoutMs );
    curl_easy_setopt( c, CURLOPT_WRITEFUNCTION, Collect );
    curl_easy_setopt( c, CURLOPT_WRITEDATA, &r.body );
    curl_easy_setopt( c, CURLOPT_FOLLOWLOCATION, 0L );
    if( !body.empty() )
    {
        curl_easy_setopt( c, CURLOPT_POSTFIELDS, body.c_str() );
        curl_easy_setopt( c, CURLOPT_POSTFIELDSIZE, (long)body.size() );
    }
    if( getenv( "JFTOOL_VERBOSE" ) )
        fprintf( stderr, "> %s %s\n%s%s\n", method, url.c_str(), extraHeaders.c_str(), body.c_str() );
    CURLcode code = curl_easy_perform( c );
    if( code != CURLE_OK )
        r.error = curl_easy_strerror( code );
    else
    {
        long status = 0;
        curl_easy_getinfo( c, CURLINFO_RESPONSE_CODE, &status );
        r.status = (int)status;
    }
    if( getenv( "JFTOOL_VERBOSE" ) )
        fprintf( stderr, "< %d %s\n%s\n", r.status, r.error.c_str(), r.body.substr( 0, 2000 ).c_str() );
    curl_slist_free_all( headers );
    curl_easy_cleanup( c );
    return r;
}

std::string Http::Escape( const std::string& s )
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for( size_t i = 0; i < s.size(); ++i )
    {
        unsigned char c = (unsigned char)s[i];
        if( ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_' ||
            c == '.' || c == '~' )
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
