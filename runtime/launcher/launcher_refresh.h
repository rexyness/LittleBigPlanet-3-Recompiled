// A community server's web API (Refresh, API v3: what play.lbppc.com runs), read-only apart from
// the player's own sign-in and "Queue this level". Routes and fields from Refresh's source
// (LittleBigRefresh/Refresh, Refresh.Interfaces.APIv3): JSON in camelCase, wrapped as
// {"success", "data", "error": {"name", "message", "statusCode"}, "listInfo"}; lists page with
// ?skip=&count=. Nothing here runs unless the player opens a page or presses a button.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace launcher {

// "https://play.lbppc.com" (as NormalizeServer gives it) -> "https://play.lbppc.com/api/v3".
std::string ApiBase(const std::string& server);
std::string UrlEncode(const std::string& text);
bool IsSha1Hex(const std::string& hash);

struct ServerInfo {
    bool ok = false;
    int ms = 0;                 // how long the answer took
    std::string name, description, software, version;
    bool registration = false, maintenance = false;
    std::string error;          // plain words when !ok
};
// GET /instance. The reachability check of the Online page.
ServerInfo PingServer(const std::string& server);
bool ParseInstance(const std::string& body, ServerInfo* out);

struct Announcement {
    std::string title, text, created;  // created: "2026-11-13"
};
// GET /announcements.
std::vector<Announcement> ParseAnnouncements(const std::string& body, std::string* error);
std::vector<Announcement> FetchAnnouncements(const std::string& server, std::string* error);

struct LevelCard {
    int id = 0;
    std::string title, description, icon_hash, publisher;
    int hearts = 0, plays = 0, yays = 0;
    bool team_picked = false;
};
// Categories of GET /levels/{route}: teamPicks, newest, mostHearted, mostLiked, mostPlayed, search.
struct LevelPage {
    std::vector<LevelCard> levels;
    int total = 0;
    std::string error;
};
std::string LevelsPath(const std::string& category, const std::string& query, int skip, int count);
LevelPage ParseLevels(const std::string& body);
LevelPage FetchLevels(const std::string& server, const std::string& category, const std::string& query, int skip, int count);

// The website sign-in: POST /login {emailAddress, passwordSha512}. A token for "Queue this level".
struct WebLogin {
    bool ok = false;
    int status = 0;
    std::string token, error;  // error: plain words
};
WebLogin RefreshLogin(const std::string& server, const std::string& email, const std::string& password);
// POST /levels/id/{id}/queue with the token: adds the level to the player's queue in the game.
bool QueueLevel(const std::string& server, const std::string& token, int level_id, std::string* error);
// A level's icon as PNG bytes (GET /assets/{sha1}/image). Hashes that aren't SHA-1 (the game's
// own "g..." icons) have no image here: empty.
std::string FetchIcon(const std::string& server, const std::string& hash, std::string* error);

// An API failure in plain words: the server's own message when it sent one, else by status
// (403: "e-mail or password not accepted", 429: "too many requests"...).
std::string ExplainApiError(int status, const std::string& body, const std::string& transport_error);

}  // namespace launcher
