#include "Platform.h"
#include <stdlib.h>
#include <stdio.h>
#include "JellyfinApi.h"
#include "Http.h"
#include "Json.h"
#include "Log.h"

namespace
{
    // Fields beyond the defaults that the browser shows.
    const char* const FIELDS = "Overview,ChildCount,RecursiveItemCount,ParentId,SeriesPrimaryImage";
    const char* const IMAGES = "EnableImageTypes=Primary,Backdrop,Thumb&ImageTypeLimit=1";

    std::string AddParam( const std::string& path, const std::string& param )
    {
        return path + ( path.find( '?' ) == std::string::npos ? "?" : "&" ) + param;
    }

    bool HasParam( const std::string& path, const char* name )
    {
        std::string q = std::string( "?" ) + name + "=", a = std::string( "&" ) + name + "=";
        return path.find( q ) != std::string::npos || path.find( a ) != std::string::npos;
    }

    std::string ItemType( const std::string& jellyfinType )
    {
        if( jellyfinType == "Movie" )       return "movie";
        if( jellyfinType == "Series" )      return "show";
        if( jellyfinType == "Season" )      return "season";
        if( jellyfinType == "Episode" )     return "episode";
        if( jellyfinType == "MusicArtist" ) return "artist";
        if( jellyfinType == "MusicAlbum" )  return "album";
        if( jellyfinType == "Audio" )       return "track";
        if( jellyfinType == "Video" || jellyfinType == "MusicVideo" || jellyfinType == "Trailer" )
            return "clip";
        return "folder";    // CollectionFolder, Folder, BoxSet, Playlist, ...
    }

    std::wstring Subtitle( const Jellyfin::Item& it )
    {
        wchar_t buf[96];
        buf[0] = 0;
        if( it.type == "movie" )
        {
            if( it.year )
                swprintf_s( buf, L"%d", it.year );
        }
        else if( it.type == "show" )
        {
            if( it.childCount )
                swprintf_s( buf, it.childCount == 1 ? L"%d season" : L"%d seasons", it.childCount );
        }
        else if( it.type == "season" )
        {
            if( it.leafCount )
                swprintf_s( buf, L"%d episodes", it.leafCount );
        }
        else if( it.type == "episode" )
        {
            swprintf_s( buf, L"S%d E%d", it.parentIndex, it.index );
        }
        else if( it.type == "album" || it.type == "track" )
        {
            return it.parentTitle;
        }
        return buf;
    }

    std::string ChildrenPath( const Jellyfin::Item& it )
    {
        std::string fields = std::string( "Fields=" ) + FIELDS + "&" + IMAGES;
        if( it.type == "show" )
            return "/Shows/" + it.id + "/Seasons?" + fields;
        if( it.type == "season" )
            return "/Shows/" + it.seriesId + "/Episodes?SeasonId=" + it.id + "&" + fields;
        std::string sort = it.type == "album" ? "SortBy=ParentIndexNumber,IndexNumber,SortName"
                                              : "SortBy=SortName&SortOrder=Ascending";
        return "/Items?ParentId=" + it.id + "&" + sort + "&" + fields;
    }

    Jellyfin::Item ParseItem( const Json::Value& j )
    {
        Jellyfin::Item it;
        std::string jtype = j["Type"].Str();
        it.type = ItemType( jtype );
        it.id = j["Id"].Str();
        it.title = j["Name"].WStr();
        it.summary = j["Overview"].WStr();
        it.parentTitle = j[it.type == "track" ? "Album" : "SeasonName"].WStr();
        it.grandparentTitle = j[it.type == "track" || it.type == "album" ? "AlbumArtist" : "SeriesName"].WStr();
        it.collectionType = j["CollectionType"].Str();
        it.contentRating = j["OfficialRating"].Str();
        it.seriesId = j["SeriesId"].Str();
        it.seasonId = j["SeasonId"].Str();
        it.year = j["ProductionYear"].Int();
        it.durationMs = (int)( j["RunTimeTicks"].Int64() / 10000 );
        it.index = j["IndexNumber"].Int();
        it.parentIndex = j["ParentIndexNumber"].Int();
        it.rating = (float)j["CommunityRating"].Num();
        it.childCount = j["ChildCount"].Int();
        it.leafCount = j["RecursiveItemCount"].Int();
        if( it.type == "season" && !it.leafCount )
            it.leafCount = it.childCount;

        const Json::Value& ud = j["UserData"];
        it.viewOffsetMs = (int)( ud["PlaybackPositionTicks"].Int64() / 10000 );
        it.played = ud["Played"].Bool();
        it.viewCount = ud["PlayCount"].Int();
        if( ud.Has( "UnplayedItemCount" ) && it.leafCount )
            it.viewedLeafCount = it.leafCount - ud["UnplayedItemCount"].Int();
        else if( it.played )
            it.viewedLeafCount = it.leafCount;

        std::string primary = j["ImageTags"]["Primary"].Str();
        if( !primary.empty() )
            it.thumb = Jellyfin::Client::ImagePath( it.id, "Primary", primary );
        if( j["BackdropImageTags"].Size() )
            it.art = Jellyfin::Client::ImagePath( it.id, "Backdrop/0", j["BackdropImageTags"][0].Str() );
        else if( j["ParentBackdropImageTags"].Size() )
            it.art = Jellyfin::Client::ImagePath( j["ParentBackdropItemId"].Str(), "Backdrop/0",
                                                  j["ParentBackdropImageTags"][0].Str() );
        if( !it.seriesId.empty() )
            it.grandparentThumb = Jellyfin::Client::ImagePath( it.seriesId, "Primary", j["SeriesPrimaryImageTag"].Str() );
        if( !it.seasonId.empty() )
            it.parentThumb = Jellyfin::Client::ImagePath( it.seasonId, "Primary", "" );
        else if( it.type == "episode" && !j["ParentThumbItemId"].Str().empty() )
            it.parentThumb = Jellyfin::Client::ImagePath( j["ParentThumbItemId"].Str(), "Thumb", j["ParentThumbImageTag"].Str() );

        it.browsable = j["IsFolder"].Bool() || it.type == "show" || it.type == "season" || it.type == "album" ||
                       it.type == "folder";
        if( it.browsable )
            it.key = ChildrenPath( it );
        it.subtitle = Subtitle( it );
        return it;
    }

    void AddItems( const Json::Value& arr, std::vector<Jellyfin::Item>& out )
    {
        for( size_t i = 0; i < arr.Size(); ++i )
            out.push_back( ParseItem( arr[i] ) );
    }

    std::string HttpError( const Http::Response& resp )
    {
        if( resp.status == 0 )
            return resp.error;
        if( resp.status == 401 )
            return "The server rejected this sign-in (401)";
        char buf[64];
        sprintf_s( buf, "Server replied HTTP %d", resp.status );
        return buf;
    }

    Jellyfin::Stream ParseStream( const Json::Value& s, int streamType )
    {
        Jellyfin::Stream st;
        st.streamType = streamType;
        st.index = s["Index"].Int( -1 );
        st.id = st.index;
        st.channels = s["Channels"].Int();
        st.codec = s["Codec"].Str();
        st.language = s["Language"].Str();
        st.title = s["DisplayTitle"].WStr();
        st.external = s["IsExternal"].Bool();
        // "English (AC3 5.1)"-style short name.
        std::wstring shortName;
        if( st.language == "eng" )
            shortName = L"English";
        else if( !s["DisplayLanguage"].Str().empty() )
            shortName = s["DisplayLanguage"].WStr();
        else if( !st.language.empty() )
            shortName = std::wstring( st.language.begin(), st.language.end() );
        else if( !s["Title"].Str().empty() )
            shortName = s["Title"].WStr();
        else
            shortName = L"Unknown";
        std::string codec = st.codec;
        for( size_t i = 0; i < codec.size(); ++i )
            if( codec[i] >= 'a' && codec[i] <= 'z' )
                codec[i] = (char)( codec[i] - 'a' + 'A' );
        wchar_t detail[64];
        detail[0] = 0;
        if( streamType == 2 )
        {
            if( st.channels == 6 )      swprintf_s( detail, L" 5.1" );
            else if( st.channels == 8 ) swprintf_s( detail, L" 7.1" );
            else if( st.channels == 2 ) swprintf_s( detail, L" 2.0" );
            else if( st.channels == 1 ) swprintf_s( detail, L" mono" );
        }
        st.shortTitle = shortName + L" (" + std::wstring( codec.begin(), codec.end() ) + detail + L")";
        if( st.title.empty() )
            st.title = st.shortTitle;
        return st;
    }

    // What the console decodes itself, and what the server should convert to otherwise.
    std::string DeviceProfile( const Jellyfin::PlayRequest& req, bool forTranscode )
    {
        int height = forTranscode ? req.maxHeight : req.directMaxHeight;
        int kbps = forTranscode ? req.maxKbps : ( req.directMaxKbps > req.maxKbps ? req.directMaxKbps : req.maxKbps );
        char buf[2048];
        sprintf_s( buf,
            "{\"Name\":\"Xbox 360\","
            "\"MaxStreamingBitrate\":%d,\"MaxStaticBitrate\":%d,\"MusicStreamingTranscodingBitrate\":192000,"
            "\"DirectPlayProfiles\":[{\"Type\":\"Video\",\"Container\":\"mkv,mp4,m4v,mov,avi,ts,mpegts,m2ts\","
              "\"VideoCodec\":\"h264,mpeg4,msmpeg4v3,mpeg2video\",\"AudioCodec\":\"aac,ac3,eac3,mp3,mp2,dca,dts,flac\"}],"
            "\"TranscodingProfiles\":[{\"Type\":\"Video\",\"Container\":\"mkv\",\"Protocol\":\"http\","
              "\"VideoCodec\":\"h264\",\"AudioCodec\":\"aac\",\"MaxAudioChannels\":\"2\",\"Context\":\"Streaming\","
              "\"CopyTimestamps\":false,\"EnableSubtitlesInManifest\":false}],"
            "\"CodecProfiles\":[{\"Type\":\"Video\",\"Codec\":\"h264\",\"Conditions\":["
              "{\"Condition\":\"LessThanEqual\",\"Property\":\"Height\",\"Value\":\"%d\",\"IsRequired\":true},"
              "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoBitrate\",\"Value\":\"%d\",\"IsRequired\":false},"
              "{\"Condition\":\"NotEquals\",\"Property\":\"IsAnamorphic\",\"Value\":\"true\",\"IsRequired\":false},"
              "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoLevel\",\"Value\":\"41\",\"IsRequired\":false}]},"
              "{\"Type\":\"Video\",\"Codec\":\"mpeg4\",\"Conditions\":["
              "{\"Condition\":\"LessThanEqual\",\"Property\":\"Height\",\"Value\":\"%d\",\"IsRequired\":true}]}],"
            "\"SubtitleProfiles\":[{\"Format\":\"srt\",\"Method\":\"Encode\"},{\"Format\":\"subrip\",\"Method\":\"Encode\"},"
              "{\"Format\":\"ass\",\"Method\":\"Encode\"},{\"Format\":\"ssa\",\"Method\":\"Encode\"},"
              "{\"Format\":\"pgssub\",\"Method\":\"Encode\"},{\"Format\":\"pgs\",\"Method\":\"Encode\"},"
              "{\"Format\":\"dvdsub\",\"Method\":\"Encode\"},{\"Format\":\"dvbsub\",\"Method\":\"Encode\"},"
              "{\"Format\":\"vtt\",\"Method\":\"Encode\"},{\"Format\":\"sub\",\"Method\":\"Encode\"},"
              "{\"Format\":\"mov_text\",\"Method\":\"Encode\"}]}",
            kbps * 1000, kbps * 1000, height, kbps * 1000, height );
        return buf;
    }
}

std::string Jellyfin::AuthHeader( const std::string& deviceId, const std::string& token )
{
    std::string h = "Authorization: MediaBrowser Client=\"Jellyfin 360\", Device=\"Xbox 360\", DeviceId=\"" +
                    deviceId + "\", Version=\"0.1\"";
    if( !token.empty() )
        h += ", Token=\"" + token + "\"";
    return h + "\r\n";
}

std::string Jellyfin::Client::Headers() const
{
    return AuthHeader( m_deviceId, m_token ) + "Accept: application/json\r\n";
}

std::string Jellyfin::Client::ImagePath( const std::string& itemId, const char* type, const std::string& tag )
{
    std::string p = "/Items/" + itemId + "/Images/" + type;
    return tag.empty() ? p : p + "?tag=" + tag;
}

std::string Jellyfin::Client::WithUser( const std::string& path ) const
{
    return HasParam( path, "userId" ) || m_userId.empty() ? path : AddParam( path, "userId=" + m_userId );
}

Jellyfin::Result Jellyfin::Client::Fetch( const std::string& path, std::string& body, int start, int count )
{
    Result r;
    std::string p = WithUser( path );
    if( count > 0 )
    {
        char page[64];
        sprintf_s( page, "StartIndex=%d&Limit=%d", start, count );
        p = AddParam( p, page );
    }
    Http::Response resp = Http::Get( m_base + p, Headers() );
    if( resp.status != 200 )
    {
        r.error = HttpError( resp );
        return r;
    }
    body.swap( resp.body );
    r.ok = true;
    return r;
}

Jellyfin::Result Jellyfin::Client::ParseList( const std::string& body, const std::string& error,
                                             const std::wstring& title )
{
    Result r;
    Json::Value j;
    std::string jsonError;
    if( !Json::Parse( body, j, jsonError ) )
    {
        r.error = "Bad reply from the server: " + jsonError;
        return r;
    }
    r.title = title;
    if( j.IsArray() )
        AddItems( j, r.items );
    else
    {
        AddItems( j["Items"], r.items );
        r.total = j["TotalRecordCount"].Int();
    }
    if( r.total < (int)r.items.size() )
        r.total = (int)r.items.size();
    r.ok = true;
    return r;
}

bool Jellyfin::Client::Ping( unsigned long timeoutMs, std::wstring& serverName, std::wstring& version,
                             std::string& error )
{
    Http::Response resp = Http::Get( m_base + "/System/Info/Public", "Accept: application/json\r\n", timeoutMs );
    if( resp.status != 200 )
    {
        error = HttpError( resp );
        return false;
    }
    Json::Value j;
    if( !Json::Parse( resp.body, j, error ) || !j.IsObject() )
    {
        error = "Not a Jellyfin server";
        return false;
    }
    serverName = j["ServerName"].WStr();
    version = j["Version"].WStr();
    return true;
}

Jellyfin::Result Jellyfin::Client::Sections()
{
    std::string body;
    Result r = Fetch( "/UserViews", body );
    if( !r.ok )
        return r;
    r = ParseList( body, r.error, L"Libraries" );
    for( size_t i = 0; i < r.items.size(); ++i )
    {
        Item& it = r.items[i];
        it.type = "folder";
        it.browsable = true;
        std::string ct = it.collectionType;
        it.subtitle = std::wstring( ct.begin(), ct.end() );
        std::string fields = std::string( "Fields=" ) + FIELDS + "&" + IMAGES;
        // Movies and shows list flat and sorted; everything else is browsed as folders.
        if( ct == "movies" )
            it.key = "/Items?ParentId=" + it.id + "&IncludeItemTypes=Movie&Recursive=true&SortBy=SortName&SortOrder=Ascending&" + fields;
        else if( ct == "tvshows" )
            it.key = "/Items?ParentId=" + it.id + "&IncludeItemTypes=Series&Recursive=true&SortBy=SortName&SortOrder=Ascending&" + fields;
        else
            it.key = "/Items?ParentId=" + it.id + "&SortBy=IsFolder,SortName&SortOrder=Ascending&" + fields;
    }
    r.total = (int)r.items.size();
    Log::Write( "Sections: %u", (unsigned)r.items.size() );
    return r;
}

Jellyfin::Result Jellyfin::Client::Browse( const std::string& path, int start, int count )
{
    std::string body;
    Result r = Fetch( path, body, start, count );
    if( !r.ok )
        return r;
    r = ParseList( body, r.error, std::wstring() );
    if( r.ok && r.total < start + (int)r.items.size() )
        r.total = start + (int)r.items.size();
    Log::Write( "Browse %s [%d+%u of %d]", path.substr( 0, path.find( '?' ) ).c_str(), start,
                (unsigned)r.items.size(), r.total );
    return r;
}

Jellyfin::Result Jellyfin::Client::Children( const Item& item, int start, int count )
{
    Result r = Browse( item.key.empty() ? ChildrenPath( item ) : item.key, start, count );
    r.title = item.title;
    return r;
}

bool Jellyfin::Client::Hubs( std::vector<Hub>& hubs, std::string& error )
{
    std::string fields = std::string( "Fields=" ) + FIELDS + "&" + IMAGES;
    struct Row { const wchar_t* title; std::string path; };
    std::vector<Row> rows;
    Row resume = { L"Continue Watching", "/UserItems/Resume?Limit=20&MediaTypes=Video&" + fields };
    Row nextUp = { L"Next Up", "/Shows/NextUp?Limit=20&EnableRewatching=false&" + fields };
    rows.push_back( resume );
    rows.push_back( nextUp );

    Result sections = Sections();
    if( !sections.ok )
    {
        error = sections.error;
        return false;
    }
    std::vector<std::wstring> titles;
    for( size_t i = 0; i < sections.items.size(); ++i )
    {
        const Item& lib = sections.items[i];
        if( lib.collectionType != "movies" && lib.collectionType != "tvshows" && lib.collectionType != "homevideos" )
            continue;
        titles.push_back( L"Recently Added in " + lib.title );
        Row latest = { 0, "/Items/Latest?ParentId=" + lib.id + "&Limit=20&" + fields };
        rows.push_back( latest );
    }

    size_t titleIdx = 0;
    for( size_t i = 0; i < rows.size(); ++i )
    {
        std::string body;
        Result r = Fetch( rows[i].path, body );
        std::wstring title = rows[i].title ? rows[i].title : titles[titleIdx++];
        if( !r.ok )
        {
            Log::Write( "Hub '%s' failed: %s", rows[i].path.substr( 0, rows[i].path.find( '?' ) ).c_str(), r.error.c_str() );
            if( i == 0 )
            {
                error = r.error;
                return false;
            }
            continue;
        }
        r = ParseList( body, r.error, title );
        if( !r.ok || r.items.empty() )
            continue;
        Hub hub;
        hub.title = title;
        hub.key = rows[i].path;
        hub.items.swap( r.items );
        hubs.push_back( hub );
    }
    Log::Write( "Hubs: %u", (unsigned)hubs.size() );
    return true;
}

Jellyfin::Result Jellyfin::Client::Details( const std::string& itemId )
{
    std::string body;
    Result r = Fetch( "/Items/" + itemId + "?Fields=" + FIELDS, body );
    if( !r.ok )
        return r;
    Json::Value j;
    if( !Json::Parse( body, j, r.error ) )
    {
        r.ok = false;
        return r;
    }
    r.items.push_back( ParseItem( j ) );
    r.title = r.items[0].title;
    r.total = 1;
    return r;
}

Jellyfin::Media Jellyfin::Client::GetMedia( const std::string& itemId, const PlayRequest& req )
{
    Media m;
    m.itemId = itemId;

    // The server decides: direct play if the file fits the profile, else it offers a conversion.
    // A transcode at a lower quality than the direct-play ceiling needs a second ask.
    bool transcodeOnly = !req.allowDirectPlay;
    std::string body;
    Json::Value j;
    for( int pass = 0; pass < 2; ++pass )
    {
        char nums[256];
        sprintf_s( nums, "\"StartTimeTicks\":%lld,\"MaxStreamingBitrate\":%d,\"SubtitleStreamIndex\":%d",
                   SecondsToTicks( req.startSeconds ),
                   ( transcodeOnly ? req.maxKbps : ( req.directMaxKbps > req.maxKbps ? req.directMaxKbps : req.maxKbps ) ) * 1000,
                   req.subtitleIndex );
        std::string audio;
        if( req.audioIndex >= 0 )
        {
            char a[48];
            sprintf_s( a, ",\"AudioStreamIndex\":%d", req.audioIndex );
            audio = a;
        }
        std::string post = std::string( "{\"DeviceProfile\":" ) + DeviceProfile( req, transcodeOnly ) + "," + nums + audio +
                           ",\"UserId\":" + Json::Quote( m_userId ) +
                           ",\"EnableDirectPlay\":" + ( transcodeOnly ? "false" : "true" ) +
                           ",\"EnableDirectStream\":" + ( transcodeOnly ? "false" : "true" ) +
                           ",\"EnableTranscoding\":true,\"AllowVideoStreamCopy\":" + ( transcodeOnly ? "false" : "true" ) +
                           ",\"AllowAudioStreamCopy\":true,\"AutoOpenLiveStream\":false}";
        Http::Response resp = Http::Request( "POST", m_base + WithUser( "/Items/" + itemId + "/PlaybackInfo" ),
                                             Headers() + "Content-Type: application/json\r\n", post, 15000 );
        if( resp.status != 200 )
        {
            m.error = HttpError( resp );
            return m;
        }
        if( !Json::Parse( resp.body, j, m.error ) )
        {
            m.error = "Bad PlaybackInfo reply: " + m.error;
            return m;
        }
        if( j["MediaSources"].Size() == 0 )
        {
            m.error = j["ErrorCode"].Str().empty() ? "This item has no playable file" : "Server: " + j["ErrorCode"].Str();
            return m;
        }
        bool direct = j["MediaSources"][0]["SupportsDirectPlay"].Bool();
        if( direct || transcodeOnly || ( req.maxKbps == req.directMaxKbps && req.maxHeight == req.directMaxHeight ) )
            break;
        transcodeOnly = true;   // ask again for the conversion at the chosen quality
    }

    const Json::Value& src = j["MediaSources"][0];
    m.playSessionId = j["PlaySessionId"].Str();
    m.mediaSourceId = src["Id"].Str();
    m.container = src["Container"].Str();
    m.bitrate = src["Bitrate"].Int() / 1000;
    m.duration = TicksToSeconds( src["RunTimeTicks"].Int64() );
    m.directPlay = !transcodeOnly && src["SupportsDirectPlay"].Bool();
    int defaultAudio = src["DefaultAudioStreamIndex"].Int( -1 );
    int defaultSub = src["DefaultSubtitleStreamIndex"].Int( -1 );
    if( req.audioIndex >= 0 )
        defaultAudio = req.audioIndex;
    if( req.subtitleIndex >= -1 )
        defaultSub = req.subtitleIndex;

    const Json::Value& streams = src["MediaStreams"];
    for( size_t i = 0; i < streams.Size(); ++i )
    {
        const Json::Value& s = streams[i];
        std::string type = s["Type"].Str();
        if( type == "Video" )
        {
            if( m.videoCodec.empty() )
            {
                m.videoCodec = s["Codec"].Str();
                m.width = s["Width"].Int();
                m.height = s["Height"].Int();
            }
            continue;
        }
        if( type != "Audio" && type != "Subtitle" )
            continue;
        Stream st = ParseStream( s, type == "Audio" ? 2 : 3 );
        st.selected = st.index == ( type == "Audio" ? defaultAudio : defaultSub );
        if( type == "Audio" && ( m.audioCodec.empty() || st.selected ) )
            m.audioCodec = st.codec;
        m.streams.push_back( st );
    }

    std::string key = "&api_key=" + m_token + "&deviceId=" + m_deviceId;
    m.directUrl = m_base + "/Videos/" + itemId + "/stream?static=true&mediaSourceId=" + m.mediaSourceId +
                  "&playSessionId=" + m.playSessionId + key;
    std::string t = src["TranscodingUrl"].Str();
    if( !t.empty() )
        m.transcodeUrl = t.compare( 0, 4, "http" ) == 0 ? t : m_base + t;

    // Title: "Show  -  S1 E2  Name" for episodes.
    Result d = Details( itemId );
    if( d.ok && !d.items.empty() )
    {
        const Item& it = d.items[0];
        m.title = it.title;
        m.viewOffset = it.viewOffsetMs / 1000.0;
        if( it.type == "episode" )
        {
            wchar_t buf[48];
            swprintf_s( buf, L"S%d E%d  ", it.parentIndex, it.index );
            m.title = it.grandparentTitle + L"  -  " + buf + it.title;
        }
    }

    Log::Write( "Media %s: %s %s/%s %dx%d %d kbps, %u tracks, direct=%d, transcode=%s", itemId.c_str(),
                m.container.c_str(), m.videoCodec.c_str(), m.audioCodec.c_str(), m.width, m.height, m.bitrate,
                (unsigned)m.streams.size(), m.directPlay ? 1 : 0, m.transcodeUrl.empty() ? "none" : "offered" );
    m.ok = true;
    return m;
}

void Jellyfin::Client::Report( const Media& media, PlayState state, double seconds, bool paused )
{
    const char* path = state == PLAY_START ? "/Sessions/Playing"
                     : state == PLAY_STOPPED ? "/Sessions/Playing/Stopped" : "/Sessions/Playing/Progress";
    char nums[128];
    sprintf_s( nums, "\"PositionTicks\":%lld,\"IsPaused\":%s,\"CanSeek\":true,\"IsMuted\":false",
               SecondsToTicks( seconds ), paused || state == PLAY_PAUSED ? "true" : "false" );
    std::string body = std::string( "{\"ItemId\":" ) + Json::Quote( media.itemId ) +
                       ",\"MediaSourceId\":" + Json::Quote( media.mediaSourceId ) +
                       ",\"PlaySessionId\":" + Json::Quote( media.playSessionId ) +
                       ",\"PlayMethod\":\"" + ( media.directPlay ? "DirectPlay" : "Transcode" ) + "\"," + nums +
                       ( state == PLAY_PROGRESS ? ",\"EventName\":\"timeupdate\"" : state == PLAY_PAUSED ? ",\"EventName\":\"pause\"" : "" ) + "}";
    Http::Response r = Http::Request( "POST", m_base + path, Headers() + "Content-Type: application/json\r\n", body, 5000 );
    if( r.status != 204 && r.status != 200 )
        Log::Write( "Report %s -> %d %s", path, r.status, r.error.c_str() );
}

void Jellyfin::Client::SetWatched( const std::string& itemId, bool watched )
{
    const char* method = watched ? "POST" : "DELETE";
    Http::Response r = Http::Request( method, m_base + WithUser( "/UserPlayedItems/" + itemId ), Headers(), std::string(), 5000 );
    if( r.status == 404 && !m_userId.empty() )   // servers before 10.9
        r = Http::Request( method, m_base + "/Users/" + m_userId + "/PlayedItems/" + itemId, Headers(), std::string(), 5000 );
    Log::Write( "SetWatched %s %d -> %d", itemId.c_str(), watched ? 1 : 0, r.status );
}

void Jellyfin::Client::StopTranscode( const std::string& playSessionId )
{
    Http::Request( "DELETE", m_base + "/Videos/ActiveEncodings?deviceId=" + m_deviceId + "&playSessionId=" + playSessionId,
                   Headers(), std::string(), 3000 );
}
