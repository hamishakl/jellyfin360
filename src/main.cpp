//--------------------------------------------------------------------------------------
// Jellyfin 360 - Jellyfin client for the Xbox 360.
//--------------------------------------------------------------------------------------
#include <xtl.h>
#include <winsockx.h>
#include "App.h"
#include "Font.h"
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>
#include <memory>
#include "Config.h"
#include "Http.h"
#include "JellyfinApi.h"
#include "JellyfinAuth.h"
#include "Log.h"
#include "FFPlayer.h"
#include "ffglue.h"
#include "Draw.h"
#include "Browser.h"
#include "Tasks.h"
#include "ImageCache.h"
#include "CpuMeter.h"
#include "Guard.h"
#include "Quality.h"
#include "PlayerScreen.h"

namespace
{
    const char* const AUTH_PATH = "game:\\auth.ini";

    const DWORD COLOR_BG_TOP     = 0xff1f1f1f;
    const DWORD COLOR_BG_BOTTOM  = 0xff0a0a0a;
    const DWORD COLOR_ACCENT     = 0xff00a4dc;   // Jellyfin blue
    const DWORD COLOR_TEXT       = 0xffeeeeee;
    const DWORD COLOR_DIM        = 0xff8a8a8a;
    const DWORD COLOR_ERROR      = 0xffff6060;


    const DWORD PING_TIMEOUT_MS  = 4000;
    const DWORD PIN_POLL_MS      = 2000;
    const DWORD PIN_GIVE_UP_MS   = 15 * 60 * 1000;


    // Work handed to the network thread; only one job runs at a time.
    struct Job
    {
        enum Kind { NONE, STARTUP, MEDIA };

        Kind          kind;
        std::string   path;
        double        offset;     // MEDIA: where to start playing
        std::wstring  title;
        Jellyfin::Result  result;
        Jellyfin::Media   media;
        volatile LONG done;

        Job() : kind( NONE ), offset( 0 ), done( 0 ) {}
    };

    std::wstring Widen( const std::string& s )
    {
        return std::wstring( s.begin(), s.end() );
    }

    std::string NewClientId()
    {
        BYTE bytes[8];
        XNetRandom( bytes, sizeof( bytes ) );
        char id[32] = "xbox360-";
        for( int i = 0; i < 8; ++i )
            sprintf_s( id + 8 + i * 2, 3, "%02x", bytes[i] );
        return id;
    }
}

class JellyfinApp : public App, public PlayerHost
{
public:
    JellyfinApp() : m_client( NULL ), m_sessionReady( false ), m_timelineTick( 0 ), m_lastPosition( 0 ),
                m_transcoding( false ), m_restarting( false ), m_reconnects( 0 ), m_startGeneration( 0 ), m_menuHeight( 720 ),
                m_qualityMode( QUALITY_AUTO ), m_qualityPreset( 1 ), m_burnSubs( false ), m_frameStart( 0 ),
                m_renderMs( 0 ), m_thread( NULL ), m_notify( NULL ), m_busy( false ) {}

private:
    virtual HRESULT Initialize();
    virtual HRESULT Update();
    virtual HRESULT Render();

    void         StartJob( Job::Kind kind, const std::string& path, const std::wstring& title );
    void         FinishJob();
    static DWORD WINAPI JobThread( LPVOID param );

    bool         SignIn( std::string& error );
    bool         ConnectToServer( std::string& error );

    void         SetStatus( const std::wstring& s );
    std::wstring Status();
    void         SetPinCode( const std::string& code );
    std::string  PinCode();

    void         RenderSignIn( const std::string& code, const D3DRECT& safe );

    Font               m_font;
    Font               m_titleFont;
    Config             m_cfg;
    AuthStore          m_auth;
    Jellyfin::Client*      m_client;
    Browser            m_browser;
    TaskQueue          m_api;           // background server requests for the browser
    ImageCache         m_images;
    UserSettings       m_settings;
    bool               m_sessionReady;  // signed in, connected, browser showing
    DWORD              m_timelineTick;  // last progress report to the server
    double             m_lastPosition;  // playback position, kept after the player stops

    void         PlayItem( const Jellyfin::Item& item, double start );
    void         SignOut();
    void         SwitchUser( const JellyfinAuth::Session& session );
    void         ApplySettings();
    void         ReportTimeline( const char* state );
    std::string        m_localIp;
    std::wstring       m_username;
    std::wstring       m_serverLabel;   // "My Server - Remote (internet)"
    std::string        m_error;

    CRITICAL_SECTION   m_lock;
    std::wstring       m_status;
    std::string        m_pinCode;

    Job                m_job;

    FFPlayer           m_player;
    PlayerScreen       m_screen;
    CpuMeter           m_cpu;

    // What to ask the server for, from the settings, the quality choice and the chosen tracks.
    Jellyfin::PlayRequest BuildRequest( double offset ) const;
    void         StartPlayback( const Jellyfin::Media& media, double offset );
    void         StopPlayback();
    void         UpdatePlayer( Pad* pad );

    // What is playing, so a transcode can be restarted at a new time for seeking.
    std::string        m_playKey;
    Jellyfin::Media        m_playMedia;
    std::wstring       m_playTitle;
    bool               m_transcoding;
    Jellyfin::PlayRequest m_playRequest;   // what the current stream was asked for

    // While the stream (re)starts the player screen stays up with a message.
    void         RestartPlayback( double position, bool selectStreams, const std::wstring& message );
    bool               m_restarting;
    int                m_reconnects;        // automatic restarts after a lost connection, this video
    int                m_startGeneration;   // bumps on every start/cancel; stale callbacks see a different value
    std::wstring       m_restartMessage;

    // PlayerHost
    virtual void         Seek( double seconds );
    virtual void         Leave();
    virtual void         CancelStart();
    virtual bool         SlowSeek() const { return m_transcoding; }
    virtual double       DurationHint() const { return m_playMedia.duration; }
    virtual std::wstring Title() const { return m_playTitle; }
    virtual std::wstring StartMessage() const { return m_restartMessage; }
    virtual std::wstring Tag() const;
    virtual IDirect3DTexture9* Preview( double seconds );
    virtual bool         ShowStats() const { return m_settings.stats; }
    virtual std::wstring StatsMode() const;
    virtual int          Brightness() const { return m_settings.brightness; }
    virtual void         BuildMenu( int page, MenuPage& out );
    virtual MenuMove     Choose( int page, int sel );
    virtual MenuMove     Back( int page );
    virtual void         SliderStep( int page, int step );
    virtual void         AspectChanged();

    // Options menu pages.
    enum { PAGE_MAIN, PAGE_QUALITY, PAGE_BITRATE, PAGE_AUDIO, PAGE_SUBS, PAGE_BRIGHTNESS, PAGE_ASPECT };
    enum { ROW_QUALITY, ROW_AUDIO, ROW_SUBS, ROW_BRIGHTNESS, ROW_ASPECT, ROW_STATS };
    enum { QUALITY_AUTO, QUALITY_ORIGINAL, QUALITY_PRESET };
    std::vector<int> TrackList( int streamType ) const;   // indexes into m_playMedia.streams; -1 = subtitles off
    std::wstring TrackName( int streamType ) const;
    std::wstring QualityName() const;
    int                m_menuHeight;        // resolution whose bitrates PAGE_BITRATE lists
    int                m_qualityMode;       // QUALITY_*: this video's choice (resets per video)
    int                m_qualityPreset;     // QUALITY_PRESETS index when QUALITY_PRESET
    bool               m_burnSubs;

    DWORD              m_frameStart;
    float              m_renderMs;
    void         RenderPlayer( const D3DRECT& safe );
    void         EndFrame();
    HANDLE             m_thread;
    HANDLE             m_notify;        // system notifications (Guide open/closed, ...)
    bool               m_busy;
};


// Separate function: __try can't live in a function with C++ destructors.
static void RunGuarded( JellyfinApp& app )
{
    __try
    {
        app.Run();
    }
    __except( Guard_Filter( GetExceptionCode(), GetExceptionInformation(), "main" ) )
    {
    }
}

VOID __cdecl main()
{
    Log::Open( "game:\\jellyfin360.log" );
    Log::Write( "Jellyfin 360 starting" );

    JellyfinApp app;
    // vsync: no tearing, and no extra frames competing with the decoder.
    app.m_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    Guard_Start();
    RunGuarded( app );
}


HRESULT JellyfinApp::Initialize()
{
    InitializeCriticalSection( &m_lock );

    m_notify = XNotifyCreateListener( XNOTIFY_SYSTEM );
    Draw::Startup( m_pd3dDevice, m_d3dpp.BackBufferWidth, m_d3dpp.BackBufferHeight );
    Font::Startup( m_pd3dDevice );
    if( FAILED( m_font.Create( "game:\\Media\\Fonts\\Text_16.fnt" ) ) ||
        FAILED( m_titleFont.Create( "game:\\Media\\Fonts\\Text_22.fnt" ) ) )
    {
        Log::Write( "Font load failed" );
        return E_FAIL;
    }
    m_font.SetWindow( SafeArea() );
    m_titleFont.SetWindow( SafeArea() );
    m_screen.Init( &m_player, &m_font, &m_titleFont, &m_cpu, this, COLOR_ACCENT );

    m_cfg.Load( "game:\\jellyfin.ini" );
    Log::Write( "Server: %s", m_cfg.server.empty() ? "(none in jellyfin.ini)" : m_cfg.server.c_str() );

    // settings.ini (Settings screen) overrides jellyfin.ini.
    m_settings.quality = m_cfg.quality;
    m_settings.kbps = m_cfg.kbps;
    m_settings.directMax = m_cfg.directMax;
    m_settings.Load( "game:\\settings.ini" );

    static const DWORD apiCpu[1] = { 1 };
    m_api.Start( apiCpu, 1 );
    m_images.Startup( m_pd3dDevice );
    BrowserHost host;
    host.play = [this]( const Jellyfin::Item& item, double start ) { PlayItem( item, start ); };
    host.signOut = [this]() { SignOut(); };
    host.exitApp = []() {
        Log::Write( "Exit to dashboard (sidebar)" );
        XLaunchNewImage( XLAUNCH_KEYWORD_DEFAULT_APP, 0 );
    };
    host.switchUser = [this]( const JellyfinAuth::Session& session ) { SwitchUser( session ); };
    host.settingsChanged = [this]() { ApplySettings(); };
    m_browser.Startup( m_pd3dDevice, &m_font, &m_titleFont, SafeArea(), &m_api, &m_images, &m_settings,
                       host );

    StartJob( Job::STARTUP, "", L"" );
    return S_OK;
}


void JellyfinApp::SetStatus( const std::wstring& s )
{
    EnterCriticalSection( &m_lock );
    m_status = s;
    LeaveCriticalSection( &m_lock );
}

std::wstring JellyfinApp::Status()
{
    EnterCriticalSection( &m_lock );
    std::wstring s = m_status;
    LeaveCriticalSection( &m_lock );
    return s;
}

void JellyfinApp::SetPinCode( const std::string& code )
{
    EnterCriticalSection( &m_lock );
    m_pinCode = code;
    LeaveCriticalSection( &m_lock );
}

std::string JellyfinApp::PinCode()
{
    EnterCriticalSection( &m_lock );
    std::string s = m_pinCode;
    LeaveCriticalSection( &m_lock );
    return s;
}


void JellyfinApp::StartJob( Job::Kind kind, const std::string& path, const std::wstring& title )
{
    m_job.kind = kind;
    m_job.path = path;
    m_job.title = title;
    m_job.result = Jellyfin::Result();
    m_job.media = Jellyfin::Media();
    m_job.done = 0;
    m_busy = true;
    m_error.clear();
    SetStatus( kind == Job::STARTUP ? ( m_client ? L"Loading libraries..." : L"Starting network..." ) : L"Loading video..." );

    m_thread = Guard_CreateThread( 0, JobThread, this, CREATE_SUSPENDED );
    XSetThreadProcessor( m_thread, 2 );   // keep the render thread's core free
    ResumeThread( m_thread );
}


bool JellyfinApp::SignIn( std::string& error )
{
    if( m_cfg.server.empty() )
    {
        error = "No server in jellyfin.ini (server=http://your-server:8096)";
        return false;
    }
    m_auth.Load( AUTH_PATH );
    if( m_auth.deviceId.empty() )
    {
        m_auth.deviceId = NewClientId();
        m_auth.Save( AUTH_PATH );
    }
    if( !m_auth.token.empty() && m_auth.server != m_cfg.server )
    {
        Log::Write( "Server changed (%s -> %s); signing in again", m_auth.server.c_str(), m_cfg.server.c_str() );
        m_auth.token.clear();
    }

    JellyfinAuth::Session session;
    if( !m_auth.token.empty() )
    {
        SetStatus( L"Checking Jellyfin sign-in..." );
        bool invalid;
        if( JellyfinAuth::CheckToken( m_cfg.server, m_auth.deviceId, m_auth.token, session, invalid, error ) )
        {
            m_username = session.username;
            m_auth.userId = session.userId;
            return true;
        }
        if( !invalid )
            return false;   // server unreachable: keep the token, let the user retry
        Log::Write( "Saved token rejected; signing in again" );
        m_auth.token.clear();
        m_auth.Save( AUTH_PATH );
    }

    if( !m_cfg.username.empty() )
    {
        SetStatus( L"Signing in as " + Widen( m_cfg.username ) + L"..." );
        if( !JellyfinAuth::Login( m_cfg.server, m_auth.deviceId, m_cfg.username, m_cfg.password, session, error ) )
            return false;
    }
    else
    {
        SetStatus( L"Asking the server for a Quick Connect code..." );
        JellyfinAuth::QuickConnect qc;
        if( !JellyfinAuth::Start( m_cfg.server, m_auth.deviceId, qc, error ) )
            return false;
        SetPinCode( qc.code );
        SetStatus( L"Waiting for you to approve the code..." );

        DWORD start = GetTickCount();
        JellyfinAuth::PinState state = JellyfinAuth::PIN_WAITING;
        while( state == JellyfinAuth::PIN_WAITING && GetTickCount() - start < PIN_GIVE_UP_MS )
        {
            Sleep( PIN_POLL_MS );
            state = JellyfinAuth::Poll( m_cfg.server, m_auth.deviceId, qc, error );
        }
        SetPinCode( "" );

        if( state != JellyfinAuth::PIN_LINKED )
        {
            if( state == JellyfinAuth::PIN_WAITING )
                error = "Sign-in timed out";
            return false;
        }
        if( !JellyfinAuth::Finish( m_cfg.server, m_auth.deviceId, qc, session, error ) )
            return false;
    }

    m_auth.server = m_cfg.server;
    m_auth.token = session.token;
    m_auth.userId = session.userId;
    m_auth.username = std::string( session.username.begin(), session.username.end() );
    m_username = session.username;
    if( !m_auth.Save( AUTH_PATH ) )
        Log::Write( "Could not save %s", AUTH_PATH );
    return true;
}


bool JellyfinApp::ConnectToServer( std::string& error )
{
    SetStatus( L"Connecting to the server..." );
    Jellyfin::Client* client = new Jellyfin::Client( m_cfg.server, m_auth.token, m_auth.deviceId, m_auth.userId );
    std::wstring name, version;
    std::string pingError;
    if( !client->Ping( PING_TIMEOUT_MS, name, version, pingError ) )
    {
        delete client;
        error = "Could not reach " + m_cfg.server + ": " + pingError;
        return false;
    }
    Http::Url u;
    u.Parse( m_cfg.server );
    Log::Write( "Connected to '%ls' (Jellyfin %ls) at %s://%s:%d", name.c_str(), version.c_str(),
                u.https ? "https" : "http", u.host.c_str(), u.port );
    m_client = client;
    m_serverLabel = name + ( u.https ? L"" : L" - unencrypted" ) + L" - Jellyfin " + version;
    return true;
}


DWORD WINAPI JellyfinApp::JobThread( LPVOID param )
{
    JellyfinApp* app = (JellyfinApp*)param;
    Job& job = app->m_job;

    switch( job.kind )
    {
    case Job::STARTUP:
        {
            std::string err;
            if( !app->m_client )
            {
                if( Http::Startup( err, app->m_localIp ) && !app->m_cfg.logTo.empty() )
                {
                    std::string to = app->m_cfg.logTo;
                    size_t colon = to.find( ':' );
                    Log::SetRemote( to.substr( 0, colon ).c_str(),
                                    colon == std::string::npos ? 5555 : atoi( to.c_str() + colon + 1 ) );
                }
                if( !err.empty() ||
                    !app->SignIn( err ) ||
                    !app->ConnectToServer( err ) )
                {
                    job.result.error = err;
                    break;
                }
            }
            app->SetStatus( L"Loading libraries..." );
            job.result = app->m_client->Sections();
        }
        break;
    case Job::MEDIA:
        job.media = app->m_client->GetMedia( job.path, app->BuildRequest( job.offset ) );
        break;
    default:
        break;
    }

    InterlockedExchange( &job.done, 1 );
    return 0;
}


void JellyfinApp::FinishJob()
{
    WaitForSingleObject( m_thread, INFINITE );
    CloseHandle( m_thread );
    m_thread = NULL;
    m_busy = false;
    SetStatus( L"" );

    if( m_job.kind == Job::MEDIA )
    {
        if( m_job.media.ok )
            StartPlayback( m_job.media, m_job.offset );
        else
            m_browser.Toast( Widen( m_job.media.error ) );
        return;
    }

    Jellyfin::Result& r = m_job.result;
    if( !r.ok )
    {
        m_error = r.error;
        Log::Write( "Error: %s", r.error.c_str() );
        return;
    }

    m_browser.SetSession( m_client, m_auth.deviceId, m_auth.token, m_username, m_serverLabel, r.items );
    m_sessionReady = true;

    if( !m_cfg.autoplay.empty() )
    {
        Log::Write( "Autoplay %s from %d s", m_cfg.autoplay.c_str(), m_cfg.autoplayStart );
        m_job.offset = m_cfg.autoplayStart;
        StartJob( Job::MEDIA, m_cfg.autoplay, L"" );
    }
}


HRESULT JellyfinApp::Update()
{
    if( m_busy && m_job.done )
        FinishJob();

    Pad* pad = ReadPads();

    Guard_Beat();

    // Log system notifications (Guide open/close etc.).
    DWORD notifyId;
    ULONG_PTR notifyParam;
    while( m_notify && XNotifyGetNext( m_notify, 0, &notifyId, &notifyParam ) )
        Log::Write( "System notification 0x%08X (param %u)", notifyId, (unsigned)notifyParam );

    // testcrash= / testhang=: crash or freeze on purpose to check that Guard recovers.
    static DWORD s_startTick = GetTickCount();
    if( m_cfg.testCrash > 0 && GetTickCount() - s_startTick > (DWORD)m_cfg.testCrash * 1000 )
    {
        Log::Write( "Test crash now" );
        RaiseException( 0xE0000001, 0, 0, NULL );
    }
    if( m_cfg.testHang > 0 && GetTickCount() - s_startTick > (DWORD)m_cfg.testHang * 1000 )
    {
        Log::Write( "Test hang now" );
        for( ;; )
            Sleep( 1000 );
    }

    // testnav=: scripted key presses, one every 1.5 s.
    static size_t s_navPos = 0;
    static DWORD s_navTick = 0;
    static DWORD s_holdUntil = 0;
    if( m_sessionReady && !m_busy && s_navPos < m_cfg.testNav.size() && GetTickCount() - s_navTick > 1500 )
    {
        s_navTick = GetTickCount();
        char k = m_cfg.testNav[s_navPos++];
        WORD key = k == 'U' ? XINPUT_GAMEPAD_DPAD_UP : k == 'D' ? XINPUT_GAMEPAD_DPAD_DOWN :
                   k == 'L' ? XINPUT_GAMEPAD_DPAD_LEFT : k == 'R' ? XINPUT_GAMEPAD_DPAD_RIGHT :
                   k == 'A' ? XINPUT_GAMEPAD_A : k == 'B' ? XINPUT_GAMEPAD_B : k == 'X' ? XINPUT_GAMEPAD_X :
                   k == 'Y' ? XINPUT_GAMEPAD_Y : k == 'H' ? XINPUT_GAMEPAD_DPAD_RIGHT : 0;
        if( key )
        {
            Log::Write( "Test key %c", k );
            pad->wPressedButtons |= key;
        }
        if( k == 'H' )
            s_holdUntil = GetTickCount() + 3000;    // H: hold Right for 3 s
    }
    if( GetTickCount() < s_holdUntil )
        pad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    WORD pressed = pad->wPressedButtons;

    if( m_player.IsActive() || m_restarting )
    {
        m_api.Pump();       // replies for a restart in progress arrive here
        UpdatePlayer( pad );
        return S_OK;
    }
    if( !m_playKey.empty() )
    {
        // Playback ended (finished, stopped or failed).
        if( m_player.Failed() && m_player.Error() == "Connection lost" && m_reconnects < 5 )
        {
            // The player's own reconnecting gave up (~20 s); start the stream again from there.
            double pos = m_player.Position();
            ++m_reconnects;
            Log::Write( "Connection lost at %.1f s, restarting (%d)", pos, m_reconnects );
            RestartPlayback( pos, false, L"Connection lost, reconnecting..." );
            return S_OK;
        }
        if( m_player.Failed() )
        {
            Log::Write( "Playback gave up: %s", m_player.Error().c_str() );
            m_browser.Toast( L"Playback failed: " + Widen( m_player.Error() ) );
        }
        ReportTimeline( "stopped" );
        if( m_transcoding && !m_playMedia.playSessionId.empty() )
        {
            Jellyfin::Client client = *m_client;
            std::string session = m_playMedia.playSessionId;
            m_api.Post( [client, session]() mutable { client.StopTranscode( session ); }, std::function<void()>() );
        }
        m_transcoding = false;
        m_playKey.clear();
        m_browser.Refresh();
    }

    if( pressed & XINPUT_GAMEPAD_BACK )
    {
        Log::Write( "Exit to dashboard" );
        XLaunchNewImage( XLAUNCH_KEYWORD_DEFAULT_APP, 0 );
    }

    if( m_sessionReady )
    {
        m_api.Pump();
        if( !m_busy )
            m_browser.Update( pad );
        return S_OK;
    }

    if( !m_busy && ( pressed & XINPUT_GAMEPAD_Y ) )
        StartJob( Job::STARTUP, "", L"" );
    return S_OK;
}

void JellyfinApp::RenderSignIn( const std::string& code, const D3DRECT& safe )
{
    float cx = (float)( safe.x2 - safe.x1 ) / 2.0f;

    m_titleFont.Begin();
    m_titleFont.DrawText( cx, 130, COLOR_TEXT, L"Sign in to Jellyfin", FONT_CENTER_X );
    m_titleFont.DrawText( cx, 190, COLOR_DIM, L"In Jellyfin on your phone or computer, open", FONT_CENTER_X );
    m_titleFont.SetScaleFactors( 1.6f, 1.6f );
    m_titleFont.DrawText( cx, 225, COLOR_ACCENT, L"your user menu > Quick Connect", FONT_CENTER_X );
    m_titleFont.SetScaleFactors( 1.0f, 1.0f );
    m_titleFont.DrawText( cx, 300, COLOR_DIM, L"and enter this code:", FONT_CENTER_X );
    m_titleFont.SetScaleFactors( 3.5f, 3.5f );
    m_titleFont.DrawText( cx, 335, COLOR_TEXT, Widen( code ).c_str(), FONT_CENTER_X );
    m_titleFont.SetScaleFactors( 1.0f, 1.0f );
    m_titleFont.End();
}


HRESULT JellyfinApp::Render()
{
    m_frameStart = GetTickCount();
    D3DRECT safe = SafeArea();
    if( m_player.IsActive() || m_restarting )
    {
        RenderPlayer( safe );
        EndFrame();
        return S_OK;
    }

    if( m_sessionReady )
    {
        m_browser.Render();
        if( m_busy )
        {
            Draw::Rect( 0, 0, 1280, 720, 0x90000000 );
            m_titleFont.Begin();
            m_titleFont.DrawText( (float)( safe.x2 - safe.x1 ) / 2, 320, COLOR_TEXT, Status().c_str(), FONT_CENTER_X );
            m_titleFont.End();
        }
        EndFrame();
        return S_OK;
    }

    FillGradient( COLOR_BG_TOP, COLOR_BG_BOTTOM );
    float width = (float)( safe.x2 - safe.x1 );
    std::string pinCode = PinCode();
    std::wstring status = Status();

    m_titleFont.Begin();
    m_titleFont.SetScaleFactors( 2.2f, 2.2f );
    m_titleFont.DrawText( width / 2, pinCode.empty() ? 230.0f : 20.0f, COLOR_ACCENT, L"JELLYFIN 360", FONT_CENTER_X );
    m_titleFont.SetScaleFactors( 1.0f, 1.0f );
    m_titleFont.End();

    if( !pinCode.empty() )
        RenderSignIn( pinCode, safe );

    m_font.Begin();
    if( !m_error.empty() )
    {
        m_font.DrawText( width / 2, 340, COLOR_ERROR, Widen( m_error ).c_str(), FONT_CENTER_X );
        m_font.DrawText( width / 2, 372, COLOR_DIM, L"Press Y to retry      BACK to exit", FONT_CENTER_X );
    }
    else if( !status.empty() )
        m_font.DrawText( width / 2, pinCode.empty() ? 340.0f : 560.0f, COLOR_DIM, status.c_str(), FONT_CENTER_X );
    if( !m_localIp.empty() )
        m_font.DrawText( width, (float)( safe.y2 - safe.y1 ) - 28, 0xff505050, ( L"Console " + Widen( m_localIp ) ).c_str(),
                         FONT_RIGHT );
    m_font.End();

    EndFrame();
    return S_OK;
}

//--------------------------------------------------------------------------------------
// Playback
//--------------------------------------------------------------------------------------
Jellyfin::PlayRequest JellyfinApp::BuildRequest( double offset ) const
{
    Jellyfin::PlayRequest r;
    r.startSeconds = offset;
    r.directMaxHeight = m_settings.directMax;
    r.directMaxKbps = m_cfg.directKbps;
    r.maxHeight = m_settings.quality;
    r.maxKbps = m_settings.kbps;
    if( m_qualityMode == QUALITY_ORIGINAL )
    {
        // Play the file as-is whatever its size; 1080p H.264 only decodes at ~9 fps on Xenon.
        r.directMaxHeight = 4320;
        r.directMaxKbps = 200000;
    }
    else if( m_qualityMode == QUALITY_PRESET )
    {
        r.allowDirectPlay = false;
        r.maxHeight = QUALITY_PRESETS[m_qualityPreset].height;
        r.maxKbps = QUALITY_PRESETS[m_qualityPreset].kbps;
    }
    const Jellyfin::Stream* a = m_playMedia.SelectedStream( 2 );
    const Jellyfin::Stream* st = m_playMedia.SelectedStream( 3 );
    r.audioIndex = a ? a->index : -1;
    // First ask: the server applies the user's subtitle preference (burned in). Once a track list
    // exists and none is selected, the user turned them off (or the server picked none).
    r.subtitleIndex = st ? st->index : ( m_playMedia.streams.empty() ? Jellyfin::SUBS_DEFAULT : Jellyfin::SUBS_OFF );
    return r;
}

void JellyfinApp::StartPlayback( const Jellyfin::Media& media, double offset )
{
    if( !m_player.Startup( m_pd3dDevice ) )
    {
        m_error = "Video playback is unavailable on this console";
        return;
    }
    fg_set_decode_threads( m_cfg.threads );
    m_player.Stop();
    // A previous conversion of this device is replaced by the new play session; tell the server.
    if( m_transcoding && !m_playMedia.playSessionId.empty() && m_playMedia.playSessionId != media.playSessionId )
    {
        Jellyfin::Client client = *m_client;
        std::string session = m_playMedia.playSessionId;
        m_api.Post( [client, session]() mutable { client.StopTranscode( session ); }, std::function<void()>() );
    }
    if( !m_restarting )
        m_restartMessage = L"Starting...";
    ++m_startGeneration;

    m_playKey = media.itemId;
    m_playMedia = media;
    m_playTitle = media.title;
    m_playRequest = BuildRequest( offset );
    m_screen.Reset();

    const Jellyfin::Stream* audio = media.SelectedStream( 2 );
    m_burnSubs = media.SelectedStream( 3 ) != NULL;
    m_transcoding = !media.directPlay;
    // Direct play: pick the track inside the file. A conversion carries one audio track.
    fg_set_audio_track( !m_transcoding && audio ? audio->index : -1 );
    Log::Write( "Playback decision: %s (quality mode %d, burn subtitles %d, %s %s/%s %dp %d kbps)",
                m_transcoding ? "transcode" : "direct", m_qualityMode, m_burnSubs ? 1 : 0, media.container.c_str(),
                media.videoCodec.c_str(), media.audioCodec.c_str(), media.height, media.bitrate );
    m_player.SetBrightness( m_settings.brightness * 0.02f );
    m_player.SetAspect( m_settings.aspect );
    m_timelineTick = GetTickCount();

    std::vector<std::string> urls;
    std::vector<std::wstring> labels;
    if( !m_transcoding )
    {
        urls.push_back( media.directUrl );
        labels.push_back( L"direct play" );
        m_player.Open( urls, labels, m_client->Headers(), offset );
    }
    else
    {
        if( media.transcodeUrl.empty() )
        {
            Log::Write( "Server offered neither direct play nor a conversion" );
            m_restarting = false;
            m_browser.Toast( L"The server can't convert this video" );
            return;
        }
        urls.push_back( media.transcodeUrl );
        wchar_t label[64];
        swprintf_s( label, L"converted by Jellyfin to %dp %g Mbps", m_playRequest.maxHeight, m_playRequest.maxKbps / 1000.0 );
        labels.push_back( label );
        // The conversion starts at 'offset', so the player shows times from there.
        m_player.Open( urls, labels, m_client->Headers(), 0.0, offset, media.duration );
    }
    m_restarting = false;
    ReportTimeline( "start" );
}

void JellyfinApp::RestartPlayback( double position, bool selectStreams, const std::wstring& message )
{
    // Every restart asks the server again: quality, tracks and start point all live in the
    // PlaybackInfo request, and a conversion can only start at a new time by being re-made.
    (void)selectStreams;
    m_restartMessage = message;
    m_restarting = true;
    m_player.Stop();
    int generation = ++m_startGeneration;
    Jellyfin::Client client = *m_client;
    std::string itemId = m_playKey;
    Jellyfin::PlayRequest req = BuildRequest( position );
    std::shared_ptr<Jellyfin::Media> media( new Jellyfin::Media );
    Log::Write( "Restart at %.0f s: audio %d, subtitles %d, direct %d, %dp %d kbps", position, req.audioIndex,
                req.subtitleIndex, req.allowDirectPlay ? 1 : 0, req.maxHeight, req.maxKbps );
    m_api.Post( [client, itemId, req, media]() mutable { *media = client.GetMedia( itemId, req ); },
                [this, generation, media, position]() {
                    if( generation != m_startGeneration || !m_restarting )
                        return;     // cancelled, or replaced by a newer start
                    if( !media->ok )
                    {
                        Log::Write( "Restart failed: %s", media->error.c_str() );
                        m_restarting = false;
                        m_browser.Toast( L"Could not restart: " + Widen( media->error ) );
                        return;
                    }
                    StartPlayback( *media, position );
                } );
}

void JellyfinApp::StopPlayback()
{
    m_player.Stop();
}

void JellyfinApp::PlayItem( const Jellyfin::Item& item, double start )
{
    if( m_busy || !m_client )
        return;
    m_job.offset = start;
    m_qualityMode = QUALITY_AUTO;    // per-video choice from the options menu
    m_reconnects = 0;
    m_playMedia = Jellyfin::Media();  // track choices start from the server's defaults
    StartJob( Job::MEDIA, item.id, item.title );
}

void JellyfinApp::ReportTimeline( const char* state )
{
    if( !m_client || m_playMedia.itemId.empty() )
        return;
    Jellyfin::Client client = *m_client;
    Jellyfin::Media media = m_playMedia;
    std::string st = state;
    Jellyfin::PlayState ps = st == "start" ? Jellyfin::PLAY_START : st == "stopped" ? Jellyfin::PLAY_STOPPED :
                             st == "paused" ? Jellyfin::PLAY_PAUSED : Jellyfin::PLAY_PROGRESS;
    double pos = m_player.IsActive() ? m_player.Position() : m_lastPosition;
    m_api.Post( [client, media, ps, pos]() mutable { client.Report( media, ps, pos, ps == Jellyfin::PLAY_PAUSED ); },
                std::function<void()>() );
}

void JellyfinApp::ApplySettings()
{
    m_player.SetBrightness( m_settings.brightness * 0.02f );
    m_player.SetAspect( m_settings.aspect );
}

void JellyfinApp::SignOut()
{
    Log::Write( "Signing out" );
    if( !m_auth.token.empty() )
        JellyfinAuth::Logout( m_cfg.server, m_auth.deviceId, m_auth.token );
    m_auth.token.clear();
    m_auth.userId.clear();
    m_auth.Save( AUTH_PATH );
    m_sessionReady = false;
    delete m_client;
    m_client = NULL;
    m_username.clear();
    m_images.Clear();
    StartJob( Job::STARTUP, "", L"" );
}

void JellyfinApp::SwitchUser( const JellyfinAuth::Session& session )
{
    Log::Write( "Switching user to %ls", session.username.c_str() );
    m_auth.token = session.token;
    m_auth.userId = session.userId;
    m_auth.username = std::string( session.username.begin(), session.username.end() );
    m_auth.Save( AUTH_PATH );
    m_sessionReady = false;
    delete m_client;
    m_client = NULL;
    m_username = session.username;
    m_images.Clear();
    StartJob( Job::STARTUP, "", L"" );
}


//--------------------------------------------------------------------------------------
// Player screen (PlayerScreen does the timeline, seeking and menu; these are the server parts)
//--------------------------------------------------------------------------------------
void JellyfinApp::Seek( double target )
{
    if( !m_transcoding )
        m_player.SeekAbsolute( target );
    else
        RestartPlayback( target, false, L"Jumping to " + FormatTime( target ) );   // a live transcode restarts there
}

void JellyfinApp::Leave()
{
    StopPlayback();
}

void JellyfinApp::CancelStart()
{
    m_restarting = false;
    ++m_startGeneration;
}

std::wstring JellyfinApp::Tag() const
{
    if( !m_transcoding )
        return L"Original";
    int h = m_settings.quality, k = m_settings.kbps;
    if( m_qualityMode == QUALITY_PRESET )
    {
        h = QUALITY_PRESETS[m_qualityPreset].height;
        k = QUALITY_PRESETS[m_qualityPreset].kbps;
    }
    wchar_t buf[48];
    swprintf_s( buf, L"%dp  %g Mbps%s", h, k / 1000.0, m_burnSubs ? L"  (subtitles)" : L"" );
    return buf;
}

IDirect3DTexture9* JellyfinApp::Preview( double seconds )
{
    // Jellyfin trickplay (10.9+) serves tiled sprite sheets, which need cropping before they
    // can be shown; not wired up yet.
    (void)seconds;
    return NULL;
}

std::wstring JellyfinApp::StatsMode() const
{
    wchar_t buf[64];
    swprintf_s( buf, L"%d decode threads   %s", m_cfg.threads, m_transcoding ? L"server transcoding" : L"direct play" );
    return buf;
}

void JellyfinApp::UpdatePlayer( Pad* pad )
{
    m_player.Update();

    // benchmark=N: play N seconds and log totals; -N: decode-only speed test.
    static DWORD s_benchStart = 0;
    static long s_benchFrom = 0;
    if( m_cfg.benchmark < 0 && m_player.IsActive() )
    {
        FFPlayer::Stats st;
        m_player.GetStats( st );
        if( !s_benchStart && st.decoded > 0 )
        {
            m_player.SetSpeedTest( true );
            s_benchStart = GetTickCount();
            s_benchFrom = st.decoded;
        }
        else if( s_benchStart && GetTickCount() - s_benchStart > (DWORD)-m_cfg.benchmark * 1000 )
        {
            float secs = ( GetTickCount() - s_benchStart ) / 1000.0f;
            Log::Write( "BENCHMARK %s speed: %.1f decoded fps over %.0f s (%ld frames, %.1f MB)", m_cfg.autoplay.c_str(),
                        ( st.decoded - s_benchFrom ) / secs, secs, st.decoded - s_benchFrom, st.bytesRead / 1048576.0 );
            m_player.SetSpeedTest( false );
            s_benchStart = 0;
            m_cfg.benchmark = 0;
            StopPlayback();
            return;
        }
    }
    if( m_cfg.benchmark > 0 && m_player.IsActive() && !m_player.IsBuffering() )
    {
        if( !s_benchStart )
        {
            s_benchStart = GetTickCount();
            double a, b, c, d;
            fg_profile( &a, &b, &c, &d, 1 );
        }
        else if( GetTickCount() - s_benchStart > (DWORD)m_cfg.benchmark * 1000 )
        {
            FFPlayer::Stats st;
            m_player.GetStats( st );
            float secs = ( GetTickCount() - s_benchStart ) / 1000.0f;
            Log::Write( "BENCHMARK %s: %.0f s, decoded %ld, shown %ld, dropped %ld, catch-ups %ld, %.1f MB",
                        m_cfg.autoplay.c_str(), secs, st.decoded, st.shown, st.dropped, st.catchups,
                        st.bytesRead / 1048576.0 );
            double cabac, recon, filter, wait;
            fg_profile( &cabac, &recon, &filter, &wait, 0 );
            double work = recon - wait;
            double total = cabac + work + filter;
            Log::Write( "BENCHMARK split per frame: cabac %.1f ms (%.0f%%), reconstruction %.1f ms (%.0f%%), loop filter %.1f ms (%.0f%%); "
                        "waiting for reference frames %.1f ms",
                        cabac / st.decoded, 100 * cabac / total, work / st.decoded, 100 * work / total,
                        filter / st.decoded, 100 * filter / total, wait / st.decoded );
            s_benchStart = 0;
            m_cfg.benchmark = 0;
            StopPlayback();
            return;
        }
    }
    if( m_player.IsActive() )
        m_lastPosition = m_player.Position();
    if( GetTickCount() - m_timelineTick > 10000 && m_player.IsActive() && !m_player.IsBuffering() )
    {
        m_timelineTick = GetTickCount();
        ReportTimeline( m_player.IsPaused() ? "paused" : "playing" );
    }

    // testseek=N: seek N seconds every 12 s, three times.
    static int s_testSeeks = 0;
    static DWORD s_testTick = GetTickCount();
    if( m_player.IsActive() && m_cfg.testSeek != 0 && s_testSeeks < 3 && GetTickCount() - s_testTick > 12000 )
    {
        s_testTick = GetTickCount();
        ++s_testSeeks;
        Log::Write( "Test seek %d: %+d s", s_testSeeks, m_cfg.testSeek );
        Seek( m_player.Position() + m_cfg.testSeek );
        return;
    }
    if( m_cfg.testOsd && m_player.IsActive() )
        m_screen.ShowFakeSeek( m_player.Position() + 754 );

    m_screen.Update( pad );
}

void JellyfinApp::RenderPlayer( const D3DRECT& safe )
{
    m_pd3dDevice->Clear( 0, NULL, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0 );
    D3DRECT full = { 0, 0, (LONG)m_d3dpp.BackBufferWidth, (LONG)m_d3dpp.BackBufferHeight };
    m_player.RenderFrame( full );
    m_screen.Render( safe, m_renderMs );
}

void JellyfinApp::EndFrame()
{
    m_renderMs = m_renderMs * 0.9f + ( GetTickCount() - m_frameStart ) * 0.1f;

    // Present() spins until vblank and starved hw thread 0's sibling; sleep most of the
    // frame first and wake ~3 ms before vblank.
    static DWORD s_lastPresent = 0;
    DWORD elapsed = GetTickCount() - s_lastPresent;
    if( elapsed < 13 )
        Sleep( 13 - elapsed );
    m_pd3dDevice->Present( NULL, NULL, NULL, NULL );
    s_lastPresent = GetTickCount();
}

//--------------------------------------------------------------------------------------
// Options menu
//--------------------------------------------------------------------------------------
std::wstring JellyfinApp::TrackName( int streamType ) const
{
    const Jellyfin::Stream* s = m_playMedia.SelectedStream( streamType );
    if( !s )
        return streamType == 3 ? L"Off" : L"Default";
    if( !s->shortTitle.empty() )
        return s->shortTitle;
    return s->title.empty() ? Widen( s->language ) : s->title;
}

std::vector<int> JellyfinApp::TrackList( int streamType ) const
{
    std::vector<int> list;
    if( streamType == 3 )
        list.push_back( -1 );       // subtitles off
    for( size_t i = 0; i < m_playMedia.streams.size(); ++i )
        if( m_playMedia.streams[i].streamType == streamType )
            list.push_back( (int)i );
    return list;
}

std::wstring JellyfinApp::QualityName() const
{
    if( m_qualityMode == QUALITY_ORIGINAL )
        return m_burnSubs ? L"Original - converted for subtitles" : L"Original";
    if( m_qualityMode == QUALITY_PRESET )
    {
        wchar_t buf[48];
        swprintf_s( buf, L"%dp  %g Mbps", QUALITY_PRESETS[m_qualityPreset].height,
                    QUALITY_PRESETS[m_qualityPreset].kbps / 1000.0 );
        return buf;
    }
    return L"Auto";
}

void JellyfinApp::BuildMenu( int page, MenuPage& out )
{
    MenuEntry e;
    wchar_t buf[48];
    switch( page )
    {
    case PAGE_MAIN:
        out.title = L"Options";
        e.opens = true;
        e.label = L"Quality";    e.value = QualityName();  out.entries.push_back( e );
        e.label = L"Audio";      e.value = TrackName( 2 ); out.entries.push_back( e );
        e.label = L"Subtitles";  e.value = TrackName( 3 ); out.entries.push_back( e );
        swprintf_s( buf, L"%+d", m_settings.brightness );
        e.label = L"Brightness"; e.value = buf;            out.entries.push_back( e );
        e.label = L"Aspect ratio"; e.value = FFPlayer::AspectName( m_settings.aspect ); out.entries.push_back( e );
        e.label = L"Aspect ratio"; e.value = FFPlayer::AspectName( m_settings.aspect ); out.entries.push_back( e );
        e.opens = false;
        e.label = L"Stats for nerds"; e.value = m_settings.stats ? L"On" : L"Off"; out.entries.push_back( e );
        break;

    case PAGE_QUALITY:
        out.title = L"Quality";
        e.label = L"Auto";      e.value = L"recommended";     e.checked = m_qualityMode == QUALITY_AUTO;     out.entries.push_back( e );
        e.label = L"Original";
        e.value = m_burnSubs ? L"not with subtitles on" : L"no conversion";
        e.checked = m_qualityMode == QUALITY_ORIGINAL;
        out.entries.push_back( e );
        for( int i = 0; i < QUALITY_HEIGHT_COUNT; ++i )
        {
            swprintf_s( buf, L"%dp", QUALITY_HEIGHTS[i] );
            e.label = buf;
            e.value = L"";
            e.opens = true;
            e.checked = m_qualityMode == QUALITY_PRESET && QUALITY_PRESETS[m_qualityPreset].height == QUALITY_HEIGHTS[i];
            out.entries.push_back( e );
        }
        break;

    case PAGE_BITRATE:
        swprintf_s( buf, L"%dp", m_menuHeight );
        out.title = buf;
        for( int i = 0; i < QUALITY_PRESET_COUNT; ++i )
            if( QUALITY_PRESETS[i].height == m_menuHeight )
            {
                e.label = QUALITY_PRESETS[i].bitrateLabel;
                e.value = L"";
                e.checked = m_qualityMode == QUALITY_PRESET && m_qualityPreset == i;
                out.entries.push_back( e );
            }
        break;

    case PAGE_AUDIO:
    case PAGE_SUBS:
        {
            int type = page == PAGE_AUDIO ? 2 : 3;
            out.title = type == 2 ? L"Audio" : L"Subtitles";
            std::vector<int> list = TrackList( type );
            const Jellyfin::Stream* selected = m_playMedia.SelectedStream( type );
            for( size_t i = 0; i < list.size(); ++i )
            {
                if( list[i] < 0 )
                {
                    e.label = L"Off";
                    e.checked = selected == NULL;
                }
                else
                {
                    const Jellyfin::Stream& s = m_playMedia.streams[list[i]];
                    e.label = s.title.empty() ? Widen( s.language ) : s.title;
                    if( e.label.empty() )
                        e.label = L"Track";
                    e.checked = s.selected;
                }
                e.value = L"";
                out.entries.push_back( e );
            }
            if( out.entries.empty() )
            {
                e.label = L"No other tracks";
                e.checked = false;
                out.entries.push_back( e );
            }
        }
        break;

    case PAGE_BRIGHTNESS:
        out.title = L"Brightness";
        out.slider = true;
        out.value = m_settings.brightness;
        out.minValue = -5;
        out.maxValue = 10;
        break;

    case PAGE_ASPECT:
        out.title = L"Aspect ratio";
        for( int i = 0; i < FFPlayer::ASPECT_COUNT; ++i )
        {
            e.label = FFPlayer::AspectName( i );
            e.checked = i == m_settings.aspect;
            out.entries.push_back( e );
        }
        break;
    }
}

MenuMove JellyfinApp::Choose( int page, int sel )
{
    double pos = m_player.Position();
    switch( page )
    {
    case PAGE_MAIN:
        switch( sel )
        {
        case ROW_QUALITY:
            {
                int start = m_qualityMode == QUALITY_PRESET ? 2 : m_qualityMode;
                if( m_qualityMode == QUALITY_PRESET )
                    for( int i = 0; i < QUALITY_HEIGHT_COUNT; ++i )
                        if( QUALITY_HEIGHTS[i] == QUALITY_PRESETS[m_qualityPreset].height )
                            start = 2 + i;
                return MenuMove::Open( PAGE_QUALITY, start );
            }
        case ROW_AUDIO:
        case ROW_SUBS:
            {
                int sub = sel == ROW_AUDIO ? PAGE_AUDIO : PAGE_SUBS;
                MenuPage list;
                BuildMenu( sub, list );
                int start = 0;
                for( size_t i = 0; i < list.entries.size(); ++i )
                    if( list.entries[i].checked )
                        start = (int)i;
                return MenuMove::Open( sub, start );
            }
        case ROW_BRIGHTNESS:
            return MenuMove::Open( PAGE_BRIGHTNESS, 0 );
        case ROW_ASPECT:
            return MenuMove::Open( PAGE_ASPECT, m_settings.aspect );
        case ROW_STATS:
            m_settings.stats = !m_settings.stats;
            m_settings.Save( "game:\\settings.ini" );
            return MenuMove::Stay();
        }
        break;

    case PAGE_QUALITY:
        if( sel < 2 )
        {
            int mode = sel == 0 ? QUALITY_AUTO : QUALITY_ORIGINAL;
            if( mode != m_qualityMode )
            {
                m_qualityMode = mode;
                Log::Write( "Quality: %s", mode == QUALITY_AUTO ? "auto" : "original" );
                RestartPlayback( pos, false, L"Changing quality..." );
            }
            return MenuMove::Close();
        }
        else
        {
            m_menuHeight = QUALITY_HEIGHTS[sel - 2];
            int start = 0, n = 0;
            for( int i = 0; i < QUALITY_PRESET_COUNT; ++i )
                if( QUALITY_PRESETS[i].height == m_menuHeight )
                {
                    if( m_qualityMode == QUALITY_PRESET && m_qualityPreset == i )
                        start = n;
                    ++n;
                }
            return MenuMove::Open( PAGE_BITRATE, start );
        }

    case PAGE_BITRATE:
        {
            int n = 0, chosen = -1;
            for( int i = 0; i < QUALITY_PRESET_COUNT; ++i )
                if( QUALITY_PRESETS[i].height == m_menuHeight && n++ == sel )
                    chosen = i;
            if( chosen >= 0 && !( m_qualityMode == QUALITY_PRESET && m_qualityPreset == chosen ) )
            {
                m_qualityMode = QUALITY_PRESET;
                m_qualityPreset = chosen;
                Log::Write( "Quality: %dp %d kbps", QUALITY_PRESETS[chosen].height, QUALITY_PRESETS[chosen].kbps );
                RestartPlayback( pos, false, L"Changing quality..." );
            }
            return MenuMove::Close();
        }

    case PAGE_AUDIO:
    case PAGE_SUBS:
        {
            int type = page == PAGE_AUDIO ? 2 : 3;
            std::vector<int> list = TrackList( type );
            if( sel >= (int)list.size() )
                return MenuMove::Close();
            int pick = list[sel];
            bool changed = false;
            for( size_t i = 0; i < m_playMedia.streams.size(); ++i )
                if( m_playMedia.streams[i].streamType == type )
                {
                    bool on = (int)i == pick;
                    changed |= m_playMedia.streams[i].selected != on;
                    m_playMedia.streams[i].selected = on;
                }
            if( changed )
                RestartPlayback( pos, true, type == 2 ? L"Changing audio..." : L"Changing subtitles..." );
            return MenuMove::Close();
        }

    case PAGE_ASPECT:
        m_player.SetAspect( sel );
        AspectChanged();
        return MenuMove::Close();
    }
    return MenuMove::Stay();
}

void JellyfinApp::AspectChanged()
{
    m_settings.aspect = m_player.Aspect();
    m_settings.Save( "game:\\settings.ini" );
}

MenuMove JellyfinApp::Back( int page )
{
    switch( page )
    {
    case PAGE_BITRATE:
        for( int i = 0; i < QUALITY_HEIGHT_COUNT; ++i )
            if( QUALITY_HEIGHTS[i] == m_menuHeight )
                return MenuMove::Open( PAGE_QUALITY, 2 + i );
        return MenuMove::Open( PAGE_QUALITY, 0 );
    case PAGE_QUALITY:    return MenuMove::Open( PAGE_MAIN, ROW_QUALITY );
    case PAGE_AUDIO:      return MenuMove::Open( PAGE_MAIN, ROW_AUDIO );
    case PAGE_SUBS:       return MenuMove::Open( PAGE_MAIN, ROW_SUBS );
    case PAGE_BRIGHTNESS: return MenuMove::Open( PAGE_MAIN, ROW_BRIGHTNESS );
    case PAGE_ASPECT:     return MenuMove::Open( PAGE_MAIN, ROW_ASPECT );
    }
    return MenuMove::Close();
}

void JellyfinApp::SliderStep( int page, int step )
{
    if( page != PAGE_BRIGHTNESS )
        return;
    m_settings.brightness = max( -5, min( 10, m_settings.brightness + step ) );
    m_settings.Save( "game:\\settings.ini" );
    ApplySettings();
}
