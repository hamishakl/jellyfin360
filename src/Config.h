#pragma once
#include <string>

// Settings from game:\jellyfin.ini (key=value lines, # comments). 'server' is required.
struct Config
{
    std::string server;       // http://192.168.1.10:8096 or https://jellyfin.example.com
    std::string username;     // optional: sign in with a password instead of Quick Connect
    std::string password;
    std::string autoplay;     // testing: item id to start playing right after connecting
    int         threads;      // video decode threads, 1-4 (default 4)
    int         directMax;    // tallest video played without the server converting it (default 720)
    int         directKbps;   // heaviest video played without converting (default 8000)
    int         quality;      // height the server converts to when a file can't be played directly (480/576/720)
    int         kbps;         // bitrate of those conversions
    int         testSeek;     // testing: seek by this many seconds every 12 s (3 times)
    bool        testOsd;      // testing: keep the player bar visible with a fake scrub target
    std::string testNav;      // testing: keys pressed one per 1.5 s (U D L R A B X Y), e.g. "R A"
    int         autoplayStart; // testing: where autoplay starts, seconds
    int         benchmark;    // testing: stop after this many seconds of playback and log a summary
    int         testCrash;    // testing: crash on purpose this many seconds after start (checks Guard)
    int         testHang;     // testing: freeze the main loop this many seconds after start
    std::string logTo;        // debugging: ip:port that receives log lines over UDP

    Config() : threads( 4 ), directMax( 720 ), directKbps( 8000 ), quality( 720 ), kbps( 4000 ), testSeek( 0 ),
               testOsd( false ), autoplayStart( 0 ), benchmark( 0 ), testCrash( 0 ), testHang( 0 ) {}

    // A missing file is fine: defaults apply.
    void Load( const char* path );
};

// Sign-in state saved on the console in game:\auth.ini.
struct AuthStore
{
    std::string deviceId;     // random, generated on first run; identifies this console to the server
    std::string server;       // the server the token belongs to
    std::string token;        // access token from Quick Connect or the password sign-in
    std::string userId;
    std::string username;

    bool Load( const char* path );
    bool Save( const char* path ) const;
};
