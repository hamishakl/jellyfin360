#!/usr/bin/env python3
"""A fake Jellyfin server with one movie library and one show, for exercising jftool / the
client without a real server.  python3 tools/mac/mockserver.py [port]  (default 8097)."""
import json, sys, re, uuid, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

TOKEN = "mocktoken"
USER = {"Id": "11111111111111111111111111111111", "Name": "Ham", "HasPassword": True, "PrimaryImageTag": "u1"}
SECRET = "mocksecret"
state = {"qc_authenticated": False, "played": set(), "reports": []}

def ticks(seconds): return int(seconds * 10_000_000)

MOVIES_LIB = {"Id": "aaaa0001", "Name": "Movies", "CollectionType": "movies", "Type": "CollectionFolder", "IsFolder": True, "ImageTags": {"Primary": "t1"}}
SHOWS_LIB  = {"Id": "aaaa0002", "Name": "Shows", "CollectionType": "tvshows", "Type": "CollectionFolder", "IsFolder": True, "ImageTags": {}}
MUSIC_LIB  = {"Id": "aaaa0003", "Name": "Music", "CollectionType": "music", "Type": "CollectionFolder", "IsFolder": True, "ImageTags": {}}

MOVIE = {"Id": "bbbb0001", "Name": "Whale Rider", "Type": "Movie", "IsFolder": False, "ProductionYear": 2002,
         "Overview": "A Māori girl... “Pai”", "RunTimeTicks": ticks(101*60), "CommunityRating": 7.6,
         "OfficialRating": "PG-13", "ImageTags": {"Primary": "m1"}, "BackdropImageTags": ["b1"],
         "UserData": {"PlaybackPositionTicks": ticks(1234), "Played": False, "PlayCount": 0}}
MOVIE2 = {"Id": "bbbb0002", "Name": "Boy", "Type": "Movie", "IsFolder": False, "ProductionYear": 2010,
          "RunTimeTicks": ticks(87*60), "ImageTags": {"Primary": "m2"}, "UserData": {"Played": True, "PlayCount": 1}}
SERIES = {"Id": "cccc0001", "Name": "Wellington Paranormal", "Type": "Series", "IsFolder": True, "ProductionYear": 2018,
          "ChildCount": 2, "RecursiveItemCount": 4, "ImageTags": {"Primary": "s1"}, "BackdropImageTags": ["sb1"],
          "UserData": {"UnplayedItemCount": 3, "Played": False}}
SEASONS = [{"Id": "dddd0001", "Name": "Season 1", "Type": "Season", "IsFolder": True, "IndexNumber": 1, "SeriesId": "cccc0001",
            "SeriesName": "Wellington Paranormal", "ChildCount": 2, "RecursiveItemCount": 2, "SeriesPrimaryImageTag": "s1",
            "UserData": {"UnplayedItemCount": 1}},
           {"Id": "dddd0002", "Name": "Season 2", "Type": "Season", "IsFolder": True, "IndexNumber": 2, "SeriesId": "cccc0001",
            "SeriesName": "Wellington Paranormal", "ChildCount": 2, "RecursiveItemCount": 2, "SeriesPrimaryImageTag": "s1",
            "UserData": {"UnplayedItemCount": 2}}]
def ep(i, season, idx, played):
    return {"Id": f"eeee000{i}", "Name": f"Episode {idx}", "Type": "Episode", "IsFolder": False, "IndexNumber": idx,
            "ParentIndexNumber": season, "SeriesId": "cccc0001", "SeasonId": f"dddd000{season}", "SeriesName": "Wellington Paranormal",
            "SeasonName": f"Season {season}", "SeriesPrimaryImageTag": "s1", "RunTimeTicks": ticks(22*60),
            "ImageTags": {"Primary": f"e{i}"}, "UserData": {"Played": played, "PlaybackPositionTicks": 0}}
EPISODES = [ep(1,1,1,True), ep(2,1,2,False), ep(3,2,1,False), ep(4,2,2,False)]
ITEMS = {x["Id"]: x for x in [MOVIES_LIB, SHOWS_LIB, MUSIC_LIB, MOVIE, MOVIE2, SERIES] + SEASONS + EPISODES}

def media_source(item, body):
    direct = body.get("EnableDirectPlay", True) and item is MOVIE   # MOVIE is h264 720p; everything else hevc 1080p
    src = {"Id": item["Id"], "Container": "mkv" if item is MOVIE else "mp4", "Bitrate": 4_500_000 if item is MOVIE else 12_000_000,
           "RunTimeTicks": item["RunTimeTicks"], "SupportsDirectPlay": direct, "SupportsDirectStream": direct, "SupportsTranscoding": True,
           "DefaultAudioStreamIndex": body.get("AudioStreamIndex", 1), "DefaultSubtitleStreamIndex": body.get("SubtitleStreamIndex", -1),
           "MediaStreams": [
               {"Type": "Video", "Index": 0, "Codec": "h264" if item is MOVIE else "hevc", "Width": 1280 if item is MOVIE else 1920, "Height": 720 if item is MOVIE else 1080},
               {"Type": "Audio", "Index": 1, "Codec": "ac3", "Channels": 6, "Language": "eng", "DisplayTitle": "English - Dolby Digital - 5.1 - Default", "IsDefault": True},
               {"Type": "Audio", "Index": 2, "Codec": "aac", "Channels": 2, "Language": "mri", "DisplayLanguage": "Māori", "DisplayTitle": "Māori - AAC - Stereo"},
               {"Type": "Subtitle", "Index": 3, "Codec": "subrip", "Language": "eng", "DisplayTitle": "English - SRT", "IsExternal": False}]}
    if not direct:
        kbps = body.get("MaxStreamingBitrate", 4_000_000) // 1000
        src["TranscodingUrl"] = (f"/videos/{item['Id']}/stream.mkv?VideoCodec=h264&AudioCodec=aac&VideoBitrate={kbps*1000-128000}"
                                 f"&AudioBitrate=128000&MaxHeight={body.get('_maxHeight', 720)}&StartTimeTicks={body.get('StartTimeTicks', 0)}"
                                 f"&SubtitleStreamIndex={body.get('SubtitleStreamIndex', -1)}&SubtitleMethod=Encode&api_key={TOKEN}&PlaySessionId=ps1")
        src["TranscodingSubProtocol"] = "http"
    return src

class H(BaseHTTPRequestHandler):
    def log_message(self, fmt, *a): sys.stderr.write("  mock: " + (fmt % a) + "\n")
    def send(self, code, obj=None, text=None):
        data = (text if text is not None else json.dumps(obj)).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(data)))
        self.end_headers(); self.wfile.write(data)
    def authed(self):
        return f'Token="{TOKEN}"' in self.headers.get("Authorization", "") or self.q.get("api_key", [""])[0] == TOKEN
    def body(self):
        n = int(self.headers.get("Content-Length", 0)); return json.loads(self.rfile.read(n) or b"{}")
    def paged(self, items):
        start = int(self.q.get("StartIndex", [0])[0]); limit = int(self.q.get("Limit", [len(items)])[0])
        return {"Items": items[start:start+limit], "TotalRecordCount": len(items), "StartIndex": start}
    def do_GET(self):
        u = urlparse(self.path); self.q = parse_qs(u.query); p = u.path.rstrip("/")
        if p == "/System/Info/Public": return self.send(200, {"ServerName": "mockbox", "Version": "10.10.7", "Id": "deadbeef"})
        if p == "/QuickConnect/Enabled": return self.send(200, text="true")
        if p == "/QuickConnect/Connect":
            if self.q.get("secret", [""])[0] != SECRET: return self.send(404, {"Message": "Unknown secret"})
            state["qc_authenticated"] = True   # a mock approves itself on first poll
            return self.send(200, {"Authenticated": True, "Secret": SECRET, "Code": "123456"})
        if p == "/Users/Public": return self.send(200, [USER, {"Id": "2222", "Name": "Guest", "HasPassword": False}])
        if not self.authed(): return self.send(401, {"Message": "Unauthorized"})
        if p == "/Users/Me": return self.send(200, USER)
        if p == "/UserViews": return self.send(200, {"Items": [MOVIES_LIB, SHOWS_LIB, MUSIC_LIB], "TotalRecordCount": 3})
        if p == "/Items":
            parent = self.q.get("ParentId", [""])[0]
            kids = {"aaaa0001": [MOVIE, MOVIE2], "aaaa0002": [SERIES], "aaaa0003": []}.get(parent, [])
            return self.send(200, self.paged(kids))
        if p == "/Items/Latest":
            parent = self.q.get("ParentId", [""])[0]
            return self.send(200, {"aaaa0001": [MOVIE2, MOVIE], "aaaa0002": [SERIES]}.get(parent, []))
        if p == "/UserItems/Resume": return self.send(200, self.paged([MOVIE]))
        if p == "/Shows/NextUp": return self.send(200, self.paged([EPISODES[1]]))
        m = re.match(r"^/Shows/([^/]+)/Seasons$", p)
        if m: return self.send(200, self.paged(SEASONS if m.group(1) == SERIES["Id"] else []))
        m = re.match(r"^/Shows/([^/]+)/Episodes$", p)
        if m:
            sid = self.q.get("SeasonId", [""])[0]
            return self.send(200, self.paged([e for e in EPISODES if e["SeasonId"] == sid]))
        m = re.match(r"^/Items/([^/]+)$", p)
        if m and m.group(1) in ITEMS: return self.send(200, ITEMS[m.group(1)])
        m = re.match(r"^/Items/([^/]+)/Images/", p)
        if m: return self.send(200, text="not-really-a-jpeg")
        m = re.match(r"^/Videos/([^/]+)/stream", p)
        if m: return self.send(200, text="not-really-a-video")
        return self.send(404, {"Message": "mock: no route " + p})
    def do_POST(self):
        u = urlparse(self.path); self.q = parse_qs(u.query); p = u.path.rstrip("/")
        if p == "/QuickConnect/Initiate": return self.send(200, {"Authenticated": False, "Secret": SECRET, "Code": "123456", "DeviceId": "x"})
        if p == "/Users/AuthenticateWithQuickConnect":
            if self.body().get("Secret") != SECRET or not state["qc_authenticated"]: return self.send(401, {})
            return self.send(200, {"User": USER, "AccessToken": TOKEN, "ServerId": "deadbeef"})
        if p == "/Users/AuthenticateByName":
            b = self.body()
            if b.get("Username") == "ham" and b.get("Pw") == "pw": return self.send(200, {"User": USER, "AccessToken": TOKEN})
            return self.send(401, {})
        if not self.authed(): return self.send(401, {"Message": "Unauthorized"})
        m = re.match(r"^/Items/([^/]+)/PlaybackInfo$", p)
        if m:
            item = ITEMS.get(m.group(1))
            if not item or item["Type"] not in ("Movie", "Episode"): return self.send(200, {"MediaSources": [], "ErrorCode": "NotAllowed"})
            b = self.body()
            prof = b.get("DeviceProfile", {})
            for cp in prof.get("CodecProfiles", []):
                for c in cp.get("Conditions", []):
                    if c.get("Property") == "Height": b["_maxHeight"] = c["Value"]
            return self.send(200, {"MediaSources": [media_source(item, b)], "PlaySessionId": "ps1"})
        if p.startswith("/Sessions/Playing"):
            state["reports"].append((p, self.body())); sys.stderr.write(f"  mock: report {p} {state['reports'][-1][1]}\n"); return self.send(204, text="")
        m = re.match(r"^/UserPlayedItems/([^/]+)$", p)
        if m: state["played"].add(m.group(1)); return self.send(200, {"Played": True})
        if p == "/Sessions/Logout": return self.send(204, text="")
        return self.send(404, {"Message": "mock: no route " + p})
    def do_DELETE(self):
        u = urlparse(self.path); self.q = parse_qs(u.query); p = u.path.rstrip("/")
        if not self.authed(): return self.send(401, {})
        m = re.match(r"^/UserPlayedItems/([^/]+)$", p)
        if m: state["played"].discard(m.group(1)); return self.send(200, {"Played": False})
        if p == "/Videos/ActiveEncodings": sys.stderr.write("  mock: transcode stopped\n"); return self.send(204, text="")
        return self.send(404, {})

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8097
    print(f"mock Jellyfin on http://127.0.0.1:{port}  (login ham / pw, or quickconnect)", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
