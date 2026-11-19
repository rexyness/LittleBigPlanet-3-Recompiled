"""A minimal MOCK of a Refresh-style LittleBigPlanet server, for testing the port's online client.

localhost only (127.0.0.1), our own code, no real server. It answers just enough of the game's
protocol to exercise the client: the login (with the MM_AUTH cookie), the start-up calls, and one
level list. Every request is logged to stdout (method, path, a short body preview); every reply is
signed as a community server signs them (X-Digest-A: HMAC-SHA1 with "CustomServerDigest" over
body + MM_AUTH cookie + path), which the game checks.

  python mock_lbp_server.py [port]      (default 10061)

/farc_hashes and /t_conf answer 410 like Refresh does, and /npdata an empty 200, so the port's
fix-ups for those (hle/http_online.cpp) are exercised too. It also answers Refresh's web API account
calls (POST /api/v3/register, PATCH /api/v3/users/me), for LBP3Launcher's Create account, and the
read-only calls launcher 3's Online and Community pages make (GET /api/v3/instance, /announcements,
/levels/<category>[?query=], /assets/<sha1>/image; POST /api/v3/login, /levels/id/<id>/queue), with
made-up levels and generated icons.
"""
import hashlib
import hmac
import http.server
import json
import re
import sys
import struct
import time
import zlib
from urllib.parse import parse_qs, urlparse

KEY = b"CustomServerDigest"
ACCOUNTS = {}  # the web API's accounts (in memory)
COOKIE = "MM_AUTH=mocksession0123456789"
LEVELS = """<slots total="1" hint_start="2"><slot type="user"><id>1</id><npHandle icon="">MockCreator</npHandle>\
<location><x>0</x><y>0</y></location><game>2</game><name>Mock Level</name><description>A level from the mock \
server</description><rootLevel>0000000000000000000000000000000000000000</rootLevel><icon></icon><initiallyLocked>\
false</initiallyLocked><isSubLevel>false</isSubLevel><isLBP1Only>false</isLBP1Only><shareable>0</shareable>\
<minPlayers>1</minPlayers><maxPlayers>4</maxPlayers><heartCount>0</heartCount><thumbsup>0</thumbsup>\
<thumbsdown>0</thumbsdown><averageRating>0</averageRating><playerCount>0</playerCount></slot></slots>"""


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def reply(self, status, body=b"", content_type="text/xml", extra=None):
        path = self.path.split("?", 1)[0]
        cookie = COOKIE.split("=", 1)[1] if "MM_AUTH" in (self.headers.get("Cookie") or "") else ""
        digest = hmac.new(KEY, body + cookie.encode() + path.encode(), hashlib.sha1).hexdigest()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("X-Digest-A", digest)
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def handle_any(self):
        n = int(self.headers.get("Content-Length") or 0)
        data = self.rfile.read(n) if n else b""
        path = self.path.split("?", 1)[0]
        head = data[:120]
        printable = all(32 <= b < 127 or b in (9, 10, 13) for b in head)
        preview = head.decode("ascii") if printable else head[:60].hex()
        print(f"{time.strftime('%H:%M:%S')} {self.command} {self.path} cookie={'yes' if 'MM_AUTH' in (self.headers.get('Cookie') or '') else 'no'} "
              f"digestA={self.headers.get('X-Digest-A','-')[:12]} body={len(data)}B {preview!r}", flush=True)
        if path.endswith("/login"):
            body = (f"<loginResult><authTicket>{COOKIE}</authTicket><lbpEnvVer>mock</lbpEnvVer></loginResult>").encode()
            return self.reply(200, body, extra={"Set-Cookie": COOKIE + "; path=/"})
        if path.endswith("/farc_hashes") or path.endswith("/t_conf"):
            return self.reply(410)
        if path.endswith("/npdata"):
            return self.reply(200)
        if path.endswith("/update_my_pins"):
            # Refresh answers with the player's pins as JSON ({"progress":[...],"awards":[...],
            # "profile_pins":[...]}); here, the ones posted. The game parses the reply, and an empty
            # body crashes it (strlen of a null pointer in its JSON parser, eboot 0x28ec0).
            return self.reply(200, data or b'{"progress":[],"awards":[],"profile_pins":[]}', "application/json")
        if path.endswith("/network_settings.nws"):
            return self.reply(200, b"ProbabilityOfPacketDelay 0.0\nMinPacketDelayFrames 0\nMaxPacketDelayFrames 3\n"
                                   b"ProbabilityOfPacketDrop 0.0\nEnableFakeConditionsForLoopback true\n"
                                   b"NumberOfFramesPredictionAllowedForNonLocalPlayer 1000\nEnablePrediction true\n"
                                   b"MinPredictedFrames 0\nMaxPredictedFrames 10\nAllowGameRendCameraSplit true\n"
                                   b"FramesBeforeAgressiveCatchup 30\nPredictionPadSides 200\nPredictionPadTop 200\n"
                                   b"PredictionPadBottom 200\nShowErrorNumbers true\nAllowModeratedLevels false\n"
                                   b"AllowModeratedPoppetItems false\nShowLevelBoos true\nTIMEOUT_DIVE_IN_TOTAL 1000000\n",
                              "text/plain")
        if "/slots" in path or path.endswith("/lolcatftw") or "/slot/" in path:
            return self.reply(200, LEVELS.encode())
        if path.endswith("/announce"):
            return self.reply(200, b"Mock server: hello from localhost", "text/plain")
        if path.startswith("/api/v3/") and self.command == "GET":
            return self.api_get(path)
        if path == "/api/v3/login" and self.command == "POST":
            return self.web_login(data)
        if re.fullmatch(r"/api/v3/levels/id/\d+/queue", path) and self.command == "POST":
            ok = (self.headers.get("Authorization") or "").startswith("mocktoken-")
            return self.api_reply(200 if ok else 401, {"success": ok} if ok else {"success": False, "error": {"message": "not signed in"}})
        if path == "/api/v3/register" and self.command == "POST":
            return self.register(data)
        if path == "/api/v3/users/me" and self.command == "PATCH":
            return self.allow_logins(data)
        return self.reply(200)

    # Refresh's web API, the two calls LBP3Launcher's "Create account" makes (accounts kept in memory).
    def api_reply(self, status, obj):
        self.reply(status, json.dumps(obj).encode(), "application/json")

    def register(self, data):
        try:
            req = json.loads(data or b"{}")
        except ValueError:
            return self.api_reply(400, {"success": False, "error": "the request was malformed"})
        name, email, digest = req.get("username", ""), req.get("emailAddress", ""), req.get("passwordSha512", "")
        if not re.fullmatch(r"[A-Za-z0-9_-]{3,16}", name) or "@" not in email or not re.fullmatch(r"[0-9a-f]{128}", digest):
            return self.api_reply(400, {"success": False, "error": "the request was invalid"})
        if name.lower() in ACCOUNTS:
            return self.api_reply(409, {"success": False, "error": "that online name is taken"})
        token = "mocktoken-" + hashlib.sha1(name.encode()).hexdigest()[:16]
        ACCOUNTS[name.lower()] = {"token": token, "allowed": False}
        print(f"  registered {name} ({email})", flush=True)
        return self.api_reply(200, {"success": True, "data": {"tokenData": token, "userId": name}})

    def allow_logins(self, data):
        token = self.headers.get("Authorization") or ""
        account = next((a for a in ACCOUNTS.values() if a["token"] == token), None)
        if account is None:
            return self.api_reply(401, {"success": False, "error": "not signed in"})
        req = json.loads(data or b"{}")
        account["allowed"] = bool(req.get("psnAuthenticationAllowed")) and bool(req.get("rpcnAuthenticationAllowed"))
        print(f"  game logins allowed: {account['allowed']}", flush=True)
        return self.api_reply(200, {"success": True, "data": {"psnAuthenticationAllowed": account["allowed"]}})

    # Launcher 3's read-only calls.
    def api_get(self, path):
        query = parse_qs(urlparse(self.path).query)
        if path == "/api/v3/instance":
            return self.api_reply(200, {"success": True, "data": {
                "instanceName": "Mock Server", "instanceDescription": "A made-up server on this PC, for the launcher's pictures.",
                "softwareName": "Refresh (mock)", "softwareVersion": "0.0", "registrationEnabled": True, "maintenanceModeEnabled": False}})
        if path == "/api/v3/announcements":
            return self.api_reply(200, {"success": True, "data": [
                {"announcementId": "1", "title": "Welcome to the mock server", "text": "Everything here is made up, for the launcher's pictures.",
                 "createdAt": "2026-11-13T12:00:00+00:00"},
                {"announcementId": "2", "title": "Weekend contest", "text": "Build a level with a cardboard theme.",
                 "createdAt": "2026-11-10T09:00:00+00:00"}]})
        m = re.fullmatch(r"/api/v3/levels/(\w+)", path)
        if m:
            levels = MOCK_LEVELS
            if m.group(1) == "search":
                q = (query.get("query") or [""])[0].lower()
                levels = [l for l in levels if q in l["title"].lower()]
            elif m.group(1) == "teamPicks":
                levels = [l for l in levels if l["teamPicked"]]
            elif m.group(1) == "mostHearted":
                levels = sorted(levels, key=lambda l: -l["hearts"])
            skip = max(0, int((query.get("skip") or ["1"])[0]) - 1)
            count = int((query.get("count") or ["20"])[0])
            return self.api_reply(200, {"success": True, "data": levels[skip:skip + count],
                                        "listInfo": {"nextPageIndex": skip + count + 1, "totalItems": len(levels)}})
        m = re.fullmatch(r"/api/v3/assets/([0-9a-f]{40})/image", path)
        if m:
            return self.reply(200, icon_png(m.group(1)), "image/png")
        return self.api_reply(404, {"success": False, "error": {"name": "ApiNotFoundError", "message": "not found"}})

    def web_login(self, data):
        req = json.loads(data or b"{}")
        if not req.get("emailAddress") or len(req.get("passwordSha512", "")) != 128:
            return self.api_reply(403, {"success": False, "error": {"message": "The email or password was incorrect.", "statusCode": 403}})
        return self.api_reply(200, {"success": True, "data": {"tokenData": "mocktoken-web", "userId": "1"}})

    do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = do_PATCH = handle_any


def icon_png(seed_hex):
    """A 128x128 PNG made from the hash: a warm gradient with a few circles (no image library)."""
    seed = bytes.fromhex(seed_hex)
    w = h = 128
    r0, g0, b0 = 90 + seed[0] % 140, 60 + seed[1] % 120, 40 + seed[2] % 100
    rows = []
    circles = [(seed[3 + i] % w, seed[6 + i] % h, 14 + seed[9 + i] % 26) for i in range(3)]
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            t = (x + y) / (w + h)
            r, g, b = int(r0 * (1 - t) + 40 * t), int(g0 * (1 - t) + 30 * t), int(b0 * (1 - t) + 70 * t)
            for cx, cy, rad in circles:
                if (x - cx) ** 2 + (y - cy) ** 2 < rad * rad:
                    r, g, b = min(255, r + 70), min(255, g + 60), min(255, b + 40)
            row += bytes((r, g, b))
        rows.append(bytes(row))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


_TITLES = ["Sackboy's Sky Garden", "The Cardboard Castle", "Toggle's Tower Climb", "Swoop Over the Bay", "Oddsock Sprint",
           "Pins and Needles", "Moon Bounce Derby", "The Great Glue Escape", "Haunted Hoardings", "Craftworld Racing",
           "Stitch in Time", "Jam Jar Jumper", "Bubble Wrap Rapids", "Paper Plane Patrol", "Felt Forest", "Button Quest"]
_MAKERS = ["Maker_Mo", "Glue_Gun", "Popit_Pro", "Stitchy", "Cardboard_Kid", "Yarn_Wizard"]
MOCK_LEVELS = [{
    "levelId": 100 + i, "title": t, "description": "A made-up level for the launcher's pictures.",
    "iconHash": hashlib.sha1(t.encode()).hexdigest(), "publisher": {"username": _MAKERS[i % len(_MAKERS)]},
    "hearts": (i * 37) % 250 + 5, "uniquePlays": (i * 113) % 2000 + 40, "yayRatings": i * 3, "teamPicked": i % 3 == 0,
} for i, t in enumerate(_TITLES)]


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 10061
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"mock LBP server on http://127.0.0.1:{port} (localhost only)", flush=True)
    srv.serve_forever()
