#pragma once
#include <string>
#include <vector>

// Signing in to a Jellyfin server: Quick Connect (a code approved from another signed-in app),
// or a username and password from jellyfin.ini.
namespace JellyfinAuth
{
    struct ServerInfo
    {
        std::wstring name;
        std::wstring version;
        std::string  id;
        bool         quickConnect;    // Quick Connect is enabled on the server

        ServerInfo() : quickConnect( false ) {}
    };

    bool GetServerInfo( const std::string& base, const std::string& deviceId, unsigned long timeoutMs,
                        ServerInfo& info, std::string& error );

    struct QuickConnect
    {
        std::string secret;
        std::string code;     // 6 digits, approved in Jellyfin: user menu -> Quick Connect
    };

    bool Start( const std::string& base, const std::string& deviceId, QuickConnect& qc, std::string& error );

    enum PinState { PIN_WAITING, PIN_LINKED, PIN_FAILED };
    PinState Poll( const std::string& base, const std::string& deviceId, const QuickConnect& qc, std::string& error );

    struct Session
    {
        std::string  token;
        std::string  userId;
        std::wstring username;
    };

    // After Poll says PIN_LINKED: swaps the secret for an access token.
    bool Finish( const std::string& base, const std::string& deviceId, const QuickConnect& qc, Session& session,
                 std::string& error );

    bool Login( const std::string& base, const std::string& deviceId, const std::string& username,
                const std::string& password, Session& session, std::string& error );

    // false + invalid=true when the server rejects the token (signed out / revoked).
    bool CheckToken( const std::string& base, const std::string& deviceId, const std::string& token,
                     Session& session, bool& invalid, std::string& error );

    // Users shown on the server's login page (Switch user screen).
    struct User
    {
        std::string  id;
        std::wstring name;
        std::string  thumb;        // image path for ImageCache, may be empty
        bool         hasPassword;

        User() : hasPassword( false ) {}
    };

    bool PublicUsers( const std::string& base, const std::string& deviceId, std::vector<User>& users,
                      std::string& error );

    void Logout( const std::string& base, const std::string& deviceId, const std::string& token );
}
