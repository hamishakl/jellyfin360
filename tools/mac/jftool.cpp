// jftool - drives src/JellyfinApi + JellyfinAuth from a Mac/PC against a real Jellyfin server,
// so the server side of the client is tested without a console. Session saved in
// ~/.jellyfin360/session.ini (server, deviceId, token, userId).
//
//   jftool info <server>                      server name/version, Quick Connect on/off
//   jftool quickconnect <server>              shows a code; approve it in Jellyfin, then saved
//   jftool login <server> <user> [password]   username/password sign-in
//   jftool me                                 check the saved token
//   jftool views | hubs | users
//   jftool browse <path> [start] [count]      any listing path, e.g. "/Items?ParentId=..."
//   jftool children <itemId>                  seasons / episodes / folder contents
//   jftool details <itemId>
//   jftool media <itemId> [options]           the play decision, tracks, URLs
//   jftool play <itemId> [options]            same, then opens the URL in mpv
//   jftool watched <itemId> 0|1
//     options: --transcode  --height N  --kbps N  --directmax N  --audio IDX  --sub IDX  --start SECONDS
//   JFTOOL_VERBOSE=1 prints every request and reply.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <map>
#include "JellyfinApi.h"
#include "JellyfinAuth.h"
#include "Http.h"
#include "Log.h"

namespace
{
    struct Session
    {
        std::string server, deviceId, token, userId;
        std::string Path() const { return std::string( getenv( "HOME" ) ) + "/.jellyfin360/session.ini"; }

        void Load()
        {
            FILE* f = fopen( Path().c_str(), "r" );
            if( !f )
                return;
            char line[1024];
            while( fgets( line, sizeof( line ), f ) )
            {
                std::string s( line );
                while( !s.empty() && ( s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r' ) )
                    s.erase( s.size() - 1 );
                size_t eq = s.find( '=' );
                if( eq == std::string::npos )
                    continue;
                std::string k = s.substr( 0, eq ), v = s.substr( eq + 1 );
                if( k == "server" ) server = v;
                else if( k == "deviceid" ) deviceId = v;
                else if( k == "token" ) token = v;
                else if( k == "userid" ) userId = v;
            }
            fclose( f );
        }

        void Save() const
        {
            mkdir( ( std::string( getenv( "HOME" ) ) + "/.jellyfin360" ).c_str(), 0700 );
            FILE* f = fopen( Path().c_str(), "w" );
            if( !f )
            {
                perror( "save session" );
                return;
            }
            fprintf( f, "server=%s\ndeviceid=%s\ntoken=%s\nuserid=%s\n", server.c_str(), deviceId.c_str(),
                     token.c_str(), userId.c_str() );
            fclose( f );
            chmod( Path().c_str(), 0600 );
        }
    };

    std::string NewDeviceId()
    {
        char id[40] = "jftool-";
        srand( (unsigned)time( NULL ) ^ (unsigned)getpid() );
        for( int i = 0; i < 16; ++i )
            sprintf( id + 7 + i, "%x", rand() & 15 );
        return id;
    }

    std::string N( const std::wstring& w )
    {
        std::string out;
        for( size_t i = 0; i < w.size(); ++i )
        {
            unsigned cp = (unsigned)w[i];
            if( cp < 0x80 ) out += (char)cp;
            else if( cp < 0x800 ) { out += (char)( 0xC0 | ( cp >> 6 ) ); out += (char)( 0x80 | ( cp & 0x3F ) ); }
            else { out += (char)( 0xE0 | ( cp >> 12 ) ); out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) ); out += (char)( 0x80 | ( cp & 0x3F ) ); }
        }
        return out;
    }

    void PrintItems( const std::vector<Jellyfin::Item>& items )
    {
        for( size_t i = 0; i < items.size(); ++i )
        {
            const Jellyfin::Item& it = items[i];
            printf( "  %-8s %s  %s%s%s%s", it.type.c_str(), it.id.c_str(), N( it.title ).c_str(),
                    it.subtitle.empty() ? "" : "  [", N( it.subtitle ).c_str(), it.subtitle.empty() ? "" : "]" );
            if( it.viewOffsetMs )
                printf( "  resume %d:%02d", it.viewOffsetMs / 60000, it.viewOffsetMs / 1000 % 60 );
            if( it.Watched() )
                printf( "  (watched)" );
            else if( it.leafCount )
                printf( "  %d/%d", it.viewedLeafCount, it.leafCount );
            if( it.browsable )
                printf( "\n           -> %s", it.key.c_str() );
            printf( "\n" );
        }
    }

    bool Usage()
    {
        fprintf( stderr, "usage: see the comment at the top of tools/mac/jftool.cpp\n" );
        return false;
    }

    Jellyfin::PlayRequest ParseOptions( int argc, char** argv, int from )
    {
        Jellyfin::PlayRequest req;
        for( int i = from; i < argc; ++i )
        {
            std::string a = argv[i];
            const char* v = i + 1 < argc ? argv[i + 1] : "0";
            if( a == "--transcode" )      req.allowDirectPlay = false;
            else if( a == "--height" )    { req.maxHeight = atoi( v ); ++i; }
            else if( a == "--kbps" )      { req.maxKbps = atoi( v ); ++i; }
            else if( a == "--directmax" ) { req.directMaxHeight = atoi( v ); ++i; }
            else if( a == "--audio" )     { req.audioIndex = atoi( v ); ++i; }
            else if( a == "--sub" )       { req.subtitleIndex = atoi( v ); ++i; }
            else if( a == "--start" )     { req.startSeconds = atof( v ); ++i; }
        }
        return req;
    }
}

int main( int argc, char** argv )
{
    if( argc < 2 )
        return Usage() ? 0 : 2;
    std::string cmd = argv[1];
    std::string error, ip;
    Http::Startup( error, ip );

    Session s;
    s.Load();
    if( s.deviceId.empty() )
        s.deviceId = NewDeviceId();

    if( cmd == "info" || cmd == "quickconnect" || cmd == "login" )
    {
        if( argc < 3 )
            return Usage() ? 0 : 2;
        std::string server = argv[2];
        while( !server.empty() && server[server.size() - 1] == '/' )
            server.erase( server.size() - 1 );
        JellyfinAuth::ServerInfo info;
        if( !JellyfinAuth::GetServerInfo( server, s.deviceId, 8000, info, error ) )
        {
            printf( "error: %s\n", error.c_str() );
            return 1;
        }
        printf( "Server: %s  version %s  id %s  quick connect: %s\n", N( info.name ).c_str(), N( info.version ).c_str(),
                info.id.c_str(), info.quickConnect ? "on" : "off" );
        if( cmd == "info" )
            return 0;

        JellyfinAuth::Session sess;
        if( cmd == "quickconnect" )
        {
            JellyfinAuth::QuickConnect qc;
            if( !JellyfinAuth::Start( server, s.deviceId, qc, error ) )
            {
                printf( "error: %s\n", error.c_str() );
                return 1;
            }
            printf( "Quick Connect code: %s   (Jellyfin: user menu > Quick Connect, enter the code)\n", qc.code.c_str() );
            JellyfinAuth::PinState st = JellyfinAuth::PIN_WAITING;
            for( int i = 0; i < 150 && st == JellyfinAuth::PIN_WAITING; ++i )
            {
                sleep( 2 );
                st = JellyfinAuth::Poll( server, s.deviceId, qc, error );
            }
            if( st != JellyfinAuth::PIN_LINKED || !JellyfinAuth::Finish( server, s.deviceId, qc, sess, error ) )
            {
                printf( "error: %s\n", error.empty() ? "timed out" : error.c_str() );
                return 1;
            }
        }
        else
        {
            if( argc < 4 )
                return Usage() ? 0 : 2;
            if( !JellyfinAuth::Login( server, s.deviceId, argv[3], argc > 4 ? argv[4] : "", sess, error ) )
            {
                printf( "error: %s\n", error.c_str() );
                return 1;
            }
        }
        s.server = server;
        s.token = sess.token;
        s.userId = sess.userId;
        s.Save();
        printf( "Signed in as %s (user %s); session saved to %s\n", N( sess.username ).c_str(), sess.userId.c_str(),
                s.Path().c_str() );
        return 0;
    }

    if( s.token.empty() )
    {
        printf( "Not signed in: run  jftool quickconnect <server>  or  jftool login <server> <user> <pw>\n" );
        return 1;
    }
    Jellyfin::Client client( s.server, s.token, s.deviceId, s.userId );

    if( cmd == "me" )
    {
        JellyfinAuth::Session sess;
        bool invalid;
        if( !JellyfinAuth::CheckToken( s.server, s.deviceId, s.token, sess, invalid, error ) )
        {
            printf( "error: %s%s\n", error.c_str(), invalid ? " (token rejected)" : "" );
            return 1;
        }
        std::wstring name, version;
        client.Ping( 5000, name, version, error );
        printf( "%s on %s (Jellyfin %s)\n", N( sess.username ).c_str(), N( name ).c_str(), N( version ).c_str() );
        return 0;
    }
    if( cmd == "views" )
    {
        Jellyfin::Result r = client.Sections();
        if( !r.ok ) { printf( "error: %s\n", r.error.c_str() ); return 1; }
        PrintItems( r.items );
        return 0;
    }
    if( cmd == "hubs" )
    {
        std::vector<Jellyfin::Hub> hubs;
        if( !client.Hubs( hubs, error ) ) { printf( "error: %s\n", error.c_str() ); return 1; }
        for( size_t i = 0; i < hubs.size(); ++i )
        {
            printf( "%s (%u)\n", N( hubs[i].title ).c_str(), (unsigned)hubs[i].items.size() );
            PrintItems( hubs[i].items );
        }
        return 0;
    }
    if( cmd == "users" )
    {
        std::vector<JellyfinAuth::User> users;
        if( !JellyfinAuth::PublicUsers( s.server, s.deviceId, users, error ) ) { printf( "error: %s\n", error.c_str() ); return 1; }
        for( size_t i = 0; i < users.size(); ++i )
            printf( "  %s  %s%s\n", users[i].id.c_str(), N( users[i].name ).c_str(), users[i].hasPassword ? "  (password)" : "" );
        return 0;
    }
    if( cmd == "browse" && argc > 2 )
    {
        Jellyfin::Result r = client.Browse( argv[2], argc > 3 ? atoi( argv[3] ) : 0, argc > 4 ? atoi( argv[4] ) : 0 );
        if( !r.ok ) { printf( "error: %s\n", r.error.c_str() ); return 1; }
        printf( "%u of %d\n", (unsigned)r.items.size(), r.total );
        PrintItems( r.items );
        return 0;
    }
    if( ( cmd == "children" || cmd == "details" ) && argc > 2 )
    {
        Jellyfin::Result d = client.Details( argv[2] );
        if( !d.ok ) { printf( "error: %s\n", d.error.c_str() ); return 1; }
        if( cmd == "details" )
        {
            PrintItems( d.items );
            const Jellyfin::Item& it = d.items[0];
            printf( "  summary: %s\n  thumb: %s\n  art: %s\n  show poster: %s\n  rating %.1f  %s  %d min\n",
                    N( it.summary ).c_str(), it.thumb.c_str(), it.art.c_str(), it.grandparentThumb.c_str(), it.rating,
                    it.contentRating.c_str(), it.durationMs / 60000 );
            return 0;
        }
        Jellyfin::Result r = client.Children( d.items[0] );
        if( !r.ok ) { printf( "error: %s\n", r.error.c_str() ); return 1; }
        printf( "%s: %u of %d\n", N( r.title ).c_str(), (unsigned)r.items.size(), r.total );
        PrintItems( r.items );
        return 0;
    }
    if( ( cmd == "media" || cmd == "play" ) && argc > 2 )
    {
        Jellyfin::PlayRequest req = ParseOptions( argc, argv, 3 );
        Jellyfin::Media m = client.GetMedia( argv[2], req );
        if( !m.ok ) { printf( "error: %s\n", m.error.c_str() ); return 1; }
        printf( "%s\n  %s  %s/%s  %dx%d  %d kbps  %.0f s  resume %.0f s\n  decision: %s\n", N( m.title ).c_str(),
                m.container.c_str(), m.videoCodec.c_str(), m.audioCodec.c_str(), m.width, m.height, m.bitrate, m.duration,
                m.viewOffset, m.directPlay ? "DIRECT PLAY" : m.transcodeUrl.empty() ? "NOTHING OFFERED" : "TRANSCODE" );
        for( size_t i = 0; i < m.streams.size(); ++i )
        {
            const Jellyfin::Stream& st = m.streams[i];
            printf( "  %s #%d %s%s  -  %s\n", st.streamType == 2 ? "audio" : "subs ", st.index, N( st.shortTitle ).c_str(),
                    st.selected ? "  *" : "", N( st.title ).c_str() );
        }
        printf( "  direct:    %s\n  transcode: %s\n  session %s\n", m.directUrl.c_str(),
                m.transcodeUrl.empty() ? "-" : m.transcodeUrl.c_str(), m.playSessionId.c_str() );
        if( cmd == "play" )
        {
            std::string url = m.directPlay ? m.directUrl : m.transcodeUrl;
            if( url.empty() ) { printf( "nothing to play\n" ); return 1; }
            client.Report( m, Jellyfin::PLAY_START, req.startSeconds, false );
            std::string c = "mpv --really-quiet --http-header-fields='" + Jellyfin::AuthHeader( s.deviceId, s.token ).substr( 0, Jellyfin::AuthHeader( s.deviceId, s.token ).size() - 2 ) + "' '" + url + "'";
            printf( "  $ %s\n", c.c_str() );
            int rc = system( c.c_str() );
            client.Report( m, Jellyfin::PLAY_STOPPED, req.startSeconds, false );
            if( !m.directPlay )
                client.StopTranscode( m.playSessionId );
            return rc == 0 ? 0 : 1;
        }
        return 0;
    }
    if( cmd == "watched" && argc > 3 )
    {
        client.SetWatched( argv[2], atoi( argv[3] ) != 0 );
        return 0;
    }
    return Usage() ? 0 : 2;
}
