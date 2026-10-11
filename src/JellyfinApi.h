#pragma once
#include <string>
#include <vector>

// Jellyfin server API (10.9 or newer). Same shapes as the Plex client this grew from, so the
// browser and player screens need few changes.
namespace Jellyfin
{
    // One thing in a list or grid: a library, movie, show, season, episode, album...
    struct Item
    {
        std::wstring title;
        std::wstring subtitle;        // year, "S1 E2", item count, ...
        std::wstring summary;
        std::wstring parentTitle;     // season / album
        std::wstring grandparentTitle;// show / artist
        std::string  id;              // Jellyfin item id
        std::string  key;             // request path listing this item's children (browsable items)
        std::string  type;            // movie, show, season, episode, artist, album, track, folder
        std::string  collectionType;  // libraries: movies, tvshows, music, homevideos, ...
        std::string  thumb;           // poster (or episode still): an image path for ImageCache
        std::string  art;             // background artwork
        std::string  parentThumb;     // season poster, for episodes
        std::string  grandparentThumb;// show poster, for episodes / seasons
        std::string  contentRating;
        std::string  seriesId;
        std::string  seasonId;
        int          year;
        int          durationMs;
        int          viewOffsetMs;    // resume point, 0 if not started
        int          viewCount;
        int          leafCount;       // episodes in a show/season
        int          viewedLeafCount;
        int          childCount;      // seasons in a show
        int          index;           // episode / season number
        int          parentIndex;     // season number of an episode
        float        rating;          // 0-10, 0 if none
        bool         played;
        bool         browsable;       // key leads to another list

        Item() : year( 0 ), durationMs( 0 ), viewOffsetMs( 0 ), viewCount( 0 ), leafCount( 0 ),
                 viewedLeafCount( 0 ), childCount( 0 ), index( 0 ), parentIndex( 0 ), rating( 0 ),
                 played( false ), browsable( false ) {}

        bool Watched() const { return leafCount ? viewedLeafCount >= leafCount : played; }
        bool Playable() const { return type == "movie" || type == "episode" || type == "clip"; }
    };

    struct Result
    {
        bool              ok;
        std::string       error;
        std::wstring      title;
        std::vector<Item> items;
        int               total;    // items available (for paging), or items.size()

        Result() : ok( false ), total( 0 ) {}
    };

    // A row on the home screen ("Continue Watching", "Next Up", "Recently Added in Movies", ...).
    struct Hub
    {
        std::wstring      title;
        std::string       key;
        std::vector<Item> items;
    };

    // An audio or subtitle track of a file.
    struct Stream
    {
        int          id;            // same as index: Jellyfin addresses tracks by container index
        int          streamType;    // 2 audio, 3 subtitles
        int          index;         // index inside the container
        int          channels;
        std::string  codec;
        std::string  language;      // language code (eng, hun, ...)
        std::wstring title;         // full description
        std::wstring shortTitle;    // "English (AC3 5.1)"
        bool         selected;
        bool         external;      // sidecar subtitle file

        Stream() : id( 0 ), streamType( 0 ), index( -1 ), channels( 0 ), selected( false ), external( false ) {}
    };

    // What is asked of the server for one play: tallest/heaviest direct play, transcode quality,
    // tracks, start point. Everything the server's decision depends on.
    struct PlayRequest
    {
        int    directMaxHeight;   // tallest H.264 the console decodes itself (720)
        int    directMaxKbps;     // heaviest file to play directly
        int    maxHeight;         // transcode target
        int    maxKbps;           // transcode target bitrate
        int    audioIndex;        // -1 = server default
        int    subtitleIndex;     // -1 = none
        bool   allowDirectPlay;   // false forces a transcode (quality presets)
        bool   burnSubtitles;     // selected subtitles drawn into the picture (needs a transcode)
        double startSeconds;

        PlayRequest() : directMaxHeight( 720 ), directMaxKbps( 8000 ), maxHeight( 720 ), maxKbps( 4000 ),
                        audioIndex( -1 ), subtitleIndex( -1 ), allowDirectPlay( true ), burnSubtitles( true ),
                        startSeconds( 0 ) {}
    };

    // The server's answer: which file, its formats and tracks, and the URLs to fetch it.
    struct Media
    {
        bool                ok;
        std::string         error;
        std::string         itemId;
        std::string         mediaSourceId;
        std::string         playSessionId;
        std::string         container;    // mkv, mp4, avi, ...
        std::string         videoCodec;   // h264, hevc, mpeg4, ...
        std::string         audioCodec;   // aac, ac3, dca, ...
        std::wstring        title;
        int                 width;
        int                 height;
        int                 bitrate;      // kbps
        double              duration;     // seconds
        double              viewOffset;   // resume point, seconds
        bool                directPlay;   // server agrees the file can be played as-is
        std::string         directUrl;    // absolute URL of the file (static stream)
        std::string         transcodeUrl; // absolute URL of the H.264/AAC conversion (empty if not offered)
        std::vector<Stream> streams;

        Media() : ok( false ), width( 0 ), height( 0 ), bitrate( 0 ), duration( 0 ), viewOffset( 0 ),
                  directPlay( false ) {}

        const Stream* SelectedStream( int streamType ) const
        {
            for( size_t i = 0; i < streams.size(); ++i )
                if( streams[i].streamType == streamType && streams[i].selected )
                    return &streams[i];
            return 0;
        }
    };

    enum PlayState { PLAY_START, PLAY_PROGRESS, PLAY_PAUSED, PLAY_STOPPED };

    class Client
    {
    public:
        // baseUrl: http://192.168.0.10:8096 or https://jellyfin.example.com (no trailing slash)
        Client( const std::string& baseUrl, const std::string& token, const std::string& deviceId,
                const std::string& userId )
            : m_base( baseUrl ), m_token( token ), m_deviceId( deviceId ), m_userId( userId ) {}

        // Quick reachability check (/System/Info/Public); returns the server's name and version.
        bool Ping( unsigned long timeoutMs, std::wstring& serverName, std::wstring& version, std::string& error );

        // The user's libraries (Movies, Shows, ...).
        Result Sections();

        // Any listing path with its query, e.g. /Items?ParentId=abc&SortBy=SortName.
        // start/count page through big libraries (count 0 = server default).
        Result Browse( const std::string& path, int start = 0, int count = 0 );

        // Children of a show (seasons) or season (episodes); folders too.
        Result Children( const Item& item, int start = 0, int count = 0 );

        // Home screen rows: Continue Watching, Next Up, Recently Added per library.
        bool   Hubs( std::vector<Hub>& hubs, std::string& error );

        // Full details of one item (summary, artwork, ...), as a one-item Result.
        Result Details( const std::string& itemId );

        // Asks the server how to play an item (direct or converted) for this request.
        Media  GetMedia( const std::string& itemId, const PlayRequest& req );

        // Playback progress for Continue Watching / watched state.
        void   Report( const Media& media, PlayState state, double seconds, bool paused );

        // Mark watched / unwatched.
        void   SetWatched( const std::string& itemId, bool watched );

        // Tells the server to stop converting (frees its CPU).
        void   StopTranscode( const std::string& playSessionId );

        // Image path for ImageCache: /Items/{id}/Images/Primary?tag=...
        static std::string ImagePath( const std::string& itemId, const char* type, const std::string& tag );

        // Headers for any request to this server (token, device identification).
        std::string Headers() const;

        const std::string& Base() const   { return m_base; }
        const std::string& Token() const  { return m_token; }
        const std::string& UserId() const { return m_userId; }

    private:
        Result      Fetch( const std::string& path, std::string& body, int start = 0, int count = 0 );
        Result      ParseList( const std::string& body, const std::string& error, const std::wstring& title );
        std::string WithUser( const std::string& path ) const;

        std::string m_base;
        std::string m_token;
        std::string m_deviceId;
        std::string m_userId;
    };

    // The Authorization header every Jellyfin request carries (token may be empty before sign-in).
    std::string AuthHeader( const std::string& deviceId, const std::string& token );

    // Jellyfin ticks are 100 ns.
    inline double    TicksToSeconds( long long ticks ) { return ticks / 10000000.0; }
    inline long long SecondsToTicks( double seconds )  { return (long long)( seconds * 10000000.0 ); }
}
