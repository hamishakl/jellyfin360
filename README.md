# Jellyfin 360

A [Jellyfin](https://jellyfin.org) client for modded (RGH/JTAG) Xbox 360 consoles. Browse your
libraries with the controller, pick up where you left off, and play video on the console itself.

It is a fork of [Simple 360 Player / Multiplex 360](https://github.com/HG-ERIK/simple360player)
(GPL-3), whose FFmpeg-on-Xenon player does the hard part. This project swaps the Plex layer for
Jellyfin's API. The plain file player (`SimplePlayer.vcxproj`) is still here, unchanged.

## Status

**Work in progress, not yet run on a console.** The Jellyfin side (sign-in, browsing, play
decisions, progress reporting) is written and tested natively on a Mac against a real server
and a mock one (see *Testing without a console*). The console build has not been compiled yet;
expect that step to shake out a few errors. Nothing here is affiliated with Jellyfin or Microsoft.

## What it does

- Sign in with **Quick Connect** (a code you approve in any signed-in Jellyfin app) or a
  username and password in `jellyfin.ini`.
- Home: Continue Watching, Next Up, Recently Added per library. Movie and TV libraries with
  posters, details, seasons and episodes. Switch user.
- Plays **H.264 up to 720p** directly on the console (FFmpeg with VMX128 code), with
  AAC/AC3/MP3/DTS audio in MKV/MP4/AVI/TS. Anything else (HEVC, 1080p, AV1, VP9...) is
  converted by your Jellyfin server to H.264 at a quality you choose, so the server's CPU or
  GPU does that work. The server makes the direct-play/transcode decision from a device profile
  the client sends (`/Items/{id}/PlaybackInfo`), the same way the official apps work.
- Audio and subtitle track selection (subtitles are burned in by the server), quality presets,
  resume, watched state, playback reporting.

## Install

1. Build it (below) or grab a release zip once there is one.
2. Copy the `Jellyfin360` folder to the console (FTP or USB), e.g. `Hdd1:\Apps\Jellyfin360`.
3. Copy `jellyfin.ini.example` to `jellyfin.ini` next to `default.xex` and set `server=`.
4. Start `default.xex` from Aurora, FreeStyle Dash or XeXMenu. The first run shows a Quick
   Connect code: in Jellyfin on your phone or PC open your user menu, choose Quick Connect and
   enter it. (Quick Connect must be on: Dashboard > General.)

The app only writes `auth.ini`, `settings.ini` and `jellyfin360.log` next to `default.xex`.
Delete `auth.ini` to sign out.

Needs Jellyfin **10.9 or newer**. Works with DashLaunch liveblock; no Xbox Live or stealth
server is needed.

## Controls

| | Browser | Playing |
|---|---|---|
| A | Open / play | Pause |
| B | Back | Back to the browser |
| Left / Right | Move | Seek 10 s (hold to go faster) |
| LB / RB | Page | Seek 5 min |
| Up / Down | Move | Show / hide the timeline |
| X | Sidebar | Aspect ratio |
| Y | Refresh | Options: quality, audio, subtitles, brightness, aspect, stats |
| Back | Exit | |

## Building for the console

Needs the Xbox 360 XDK (2.0.21256) and Visual Studio 2010 SP1 on Windows (a VM is fine).
Build FFmpeg first (`external/ffmpeg360/vcproj/libavcodec/libavcodec.vcxproj`), then

```
.\tools\build.ps1            # -> build\Release\default.xex
.\tools\run.ps1              # copies it + jellyfin.ini to hdd:\Jellyfin360 over xbdm and launches it
```

`tools\loglisten.ps1` receives the console's log over UDP (`logto=` in `jellyfin.ini`).

## Testing without a console

Everything that talks to the server (`src/Json.*`, `src/JellyfinApi.*`, `src/JellyfinAuth.*`)
is plain C++ that also builds on a Mac or Linux box with libcurl, so the API layer can be
exercised and debugged there:

```
cmake -S tools/mac -B build/mac && cmake --build build/mac
build/mac/jsontest                                   # parser self-test
build/mac/jftool quickconnect http://server:8096     # or: jftool login http://server:8096 user pw
build/mac/jftool hubs
build/mac/jftool media <itemId>                      # the play decision, tracks and stream URLs
build/mac/jftool media <itemId> --transcode --height 480 --kbps 2000 --sub 3
build/mac/jftool play <itemId>                       # opens the exact URL the console would, in mpv
```

`python3 tools/mac/mockserver.py` runs a fake Jellyfin on port 8097 (login `ham` / `pw`) with
a movie library and a show, for working on the client with no server at hand. The session is
saved in `~/.jellyfin360/session.ini`; `JFTOOL_VERBOSE=1` prints every request and reply.

Video decoding and rendering only run on the console. For UI work, Xenia Canary can run the
XEX, though its networking is unreliable.

## Not done yet

- Compile and run on a console; fix what that finds.
- Server discovery on the LAN (Jellyfin answers UDP broadcasts on port 7359) so `server=` is
  not needed; an on-screen address entry for consoles without a keyboard.
- Trickplay thumbnails while seeking (Jellyfin serves tiled sprite sheets).
- Music, home videos and collections browse as folders only.
- Direct play of files with embedded text subtitles without a transcode (the file player
  already has a subtitle renderer).

## Credits

- [HG-ERIK](https://github.com/HG-ERIK) for Simple 360 Player and Multiplex 360, which this
  is built on.
- [FFmpeg](https://ffmpeg.org) 0.7, from the FFPlay360 port
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) 2.16
- [Inter](https://rsms.me/inter/) font (SIL Open Font License)

Licensed under the GPLv3.
