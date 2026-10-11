#include "Platform.h"
#include <stdio.h>
#include "JellyfinAuth.h"
#include "JellyfinApi.h"
#include "Http.h"
#include "Json.h"
#include "Log.h"

namespace
{
    std::string HttpError( const Http::Response& r )
    {
        if( r.status == 0 )
            return r.error;
        char buf[64];
        sprintf_s( buf, "The server replied HTTP %d", r.status );
        return buf;
    }

    std::string Headers( const std::string& deviceId, const std::string& token = std::string() )
    {
        return Jellyfin::AuthHeader( deviceId, token ) + "Accept: application/json\r\n";
    }

    bool ParseSession( const Json::Value& j, JellyfinAuth::Session& s )
    {
        s.token = j["AccessToken"].Str();
        s.userId = j["User"]["Id"].Str();
        s.username = j["User"]["Name"].WStr();
        return !s.token.empty() && !s.userId.empty();
    }
}

bool JellyfinAuth::GetServerInfo( const std::string& base, const std::string& deviceId, unsigned long timeoutMs,
                                  ServerInfo& info, std::string& error )
{
    Http::Response r = Http::Get( base + "/System/Info/Public", Headers( deviceId ), timeoutMs );
    if( r.status != 200 )
    {
        error = HttpError( r );
        return false;
    }
    Json::Value j;
    if( !Json::Parse( r.body, j, error ) || !j.IsObject() || j["Id"].Str().empty() )
    {
        error = "That address is not a Jellyfin server";
        return false;
    }
    info.name = j["ServerName"].WStr();
    info.version = j["Version"].WStr();
    info.id = j["Id"].Str();

    Http::Response q = Http::Get( base + "/QuickConnect/Enabled", Headers( deviceId ), timeoutMs );
    info.quickConnect = q.status == 200 && q.body.find( "true" ) != std::string::npos;
    Log::Write( "Server '%ls' %ls, quick connect %s", info.name.c_str(), info.version.c_str(),
                info.quickConnect ? "on" : "off" );
    return true;
}

bool JellyfinAuth::Start( const std::string& base, const std::string& deviceId, QuickConnect& qc, std::string& error )
{
    Http::Response r = Http::Request( "POST", base + "/QuickConnect/Initiate", Headers( deviceId ) );
    if( r.status == 401 )
    {
        error = "Quick Connect is turned off on the server (Dashboard > General)";
        return false;
    }
    if( r.status != 200 )
    {
        error = HttpError( r );
        return false;
    }
    Json::Value j;
    if( !Json::Parse( r.body, j, error ) )
        return false;
    qc.secret = j["Secret"].Str();
    qc.code = j["Code"].Str();
    if( qc.secret.empty() || qc.code.empty() )
    {
        error = "The server sent no Quick Connect code";
        return false;
    }
    Log::Write( "Quick Connect code created" );
    return true;
}

JellyfinAuth::PinState JellyfinAuth::Poll( const std::string& base, const std::string& deviceId,
                                           const QuickConnect& qc, std::string& error )
{
    Http::Response r = Http::Get( base + "/QuickConnect/Connect?secret=" + Http::Escape( qc.secret ), Headers( deviceId ) );
    if( r.status == 404 )
    {
        error = "The Quick Connect code expired";
        return PIN_FAILED;
    }
    if( r.status != 200 )
    {
        // Network hiccups while waiting aren't fatal; keep polling.
        Log::Write( "Quick Connect poll: %s", HttpError( r ).c_str() );
        return PIN_WAITING;
    }
    Json::Value j;
    if( !Json::Parse( r.body, j, error ) )
        return PIN_WAITING;
    return j["Authenticated"].Bool() ? PIN_LINKED : PIN_WAITING;
}

bool JellyfinAuth::Finish( const std::string& base, const std::string& deviceId, const QuickConnect& qc,
                           Session& session, std::string& error )
{
    std::string body = "{\"Secret\":" + Json::Quote( qc.secret ) + "}";
    Http::Response r = Http::Request( "POST", base + "/Users/AuthenticateWithQuickConnect",
                                      Headers( deviceId ) + "Content-Type: application/json\r\n", body );
    if( r.status != 200 )
    {
        error = HttpError( r );
        return false;
    }
    Json::Value j;
    if( !Json::Parse( r.body, j, error ) || !ParseSession( j, session ) )
    {
        error = "The server sent no access token";
        return false;
    }
    Log::Write( "Signed in with Quick Connect as %ls", session.username.c_str() );
    return true;
}

bool JellyfinAuth::Login( const std::string& base, const std::string& deviceId, const std::string& username,
                         const std::string& password, Session& session, std::string& error )
{
    std::string body = "{\"Username\":" + Json::Quote( username ) + ",\"Pw\":" + Json::Quote( password ) + "}";
    Http::Response r = Http::Request( "POST", base + "/Users/AuthenticateByName",
                                      Headers( deviceId ) + "Content-Type: application/json\r\n", body );
    if( r.status == 401 )
    {
        error = "Wrong username or password";
        return false;
    }
    if( r.status != 200 )
    {
        error = HttpError( r );
        return false;
    }
    Json::Value j;
    if( !Json::Parse( r.body, j, error ) || !ParseSession( j, session ) )
    {
        error = "The server sent no access token";
        return false;
    }
    Log::Write( "Signed in as %ls", session.username.c_str() );
    return true;
}

bool JellyfinAuth::CheckToken( const std::string& base, const std::string& deviceId, const std::string& token,
                               Session& session, bool& invalid, std::string& error )
{
    invalid = false;
    Http::Response r = Http::Get( base + "/Users/Me", Headers( deviceId, token ) );
    if( r.status == 401 )
    {
        invalid = true;
        error = "Jellyfin sign-in expired";
        return false;
    }
    if( r.status != 200 )
    {
        error = HttpError( r );
        return false;
    }
    Json::Value j;
    if( !Json::Parse( r.body, j, error ) )
        return false;
    session.token = token;
    session.userId = j["Id"].Str();
    session.username = j["Name"].WStr();
    return !session.userId.empty();
}

bool JellyfinAuth::PublicUsers( const std::string& base, const std::string& deviceId, std::vector<User>& users,
                                std::string& error )
{
    Http::Response r = Http::Get( base + "/Users/Public", Headers( deviceId ) );
    if( r.status != 200 )
    {
        error = HttpError( r );
        return false;
    }
    Json::Value j;
    if( !Json::Parse( r.body, j, error ) )
        return false;
    for( size_t i = 0; i < j.Size(); ++i )
    {
        User u;
        u.id = j[i]["Id"].Str();
        u.name = j[i]["Name"].WStr();
        u.hasPassword = j[i]["HasPassword"].Bool();
        std::string tag = j[i]["PrimaryImageTag"].Str();
        if( !tag.empty() )
            u.thumb = "/Users/" + u.id + "/Images/Primary?tag=" + tag;
        if( !u.id.empty() )
            users.push_back( u );
    }
    Log::Write( "Public users: %u", (unsigned)users.size() );
    return true;
}

void JellyfinAuth::Logout( const std::string& base, const std::string& deviceId, const std::string& token )
{
    Http::Request( "POST", base + "/Sessions/Logout", Headers( deviceId, token ), std::string(), 5000 );
}
