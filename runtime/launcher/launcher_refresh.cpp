// A community server's web API (launcher_refresh.h).
#include "launcher_refresh.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cctype>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "launcher_account.h"  // HttpJson, Sha512Hex, JsonEscape

namespace launcher {
using json = nlohmann::json;

namespace {

std::string Str(const json& o, const char* key) {
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}
int Int(const json& o, const char* key) {
    auto it = o.find(key);
    return it != o.end() && it->is_number() ? it->get<int>() : 0;
}
bool Bool(const json& o, const char* key) {
    auto it = o.find(key);
    return it != o.end() && it->is_boolean() && it->get<bool>();
}
// {"success": true, "data": ...} -> data (null on failure).
const json* Data(const json& j) {
    if (!j.is_object() || !Bool(j, "success")) return nullptr;
    auto it = j.find("data");
    return it == j.end() ? nullptr : &*it;
}
json Parse(const std::string& body) { return json::parse(body, nullptr, false); }

}  // namespace

std::string ApiBase(const std::string& server) {
    std::string s = server;
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s + "/api/v3";
}

std::string UrlEncode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

bool IsSha1Hex(const std::string& h) {
    if (h.size() != 40) return false;
    for (char c : h)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::string ExplainApiError(int status, const std::string& body, const std::string& transport) {
    if (status == 0) return transport.empty() ? "the server didn't answer" : "the server didn't answer (" + transport + ")";
    std::string message;
    const json j = Parse(body);
    if (j.is_object()) {
        if (auto e = j.find("error"); e != j.end() && e->is_object()) message = Str(*e, "message");
    }
    switch (status) {
    case 403:
        return "the server didn't accept the e-mail or password (it answers the same for both)" +
               (message.empty() ? std::string() : ": \"" + message + "\"");
    case 401: return "you need to sign in first";
    case 404: return message.empty() ? "not found on this server" : message;
    case 429: return "too many requests; wait a minute and try again";
    case 503: return "the server is down for maintenance" + (message.empty() ? std::string() : ": " + message);
    default: break;
    }
    if (status >= 500) return "the server had a problem (HTTP " + std::to_string(status) + ")";
    return message.empty() ? "the server said no (HTTP " + std::to_string(status) + ")" : message;
}

bool ParseInstance(const std::string& body, ServerInfo* out) {
    const json j = Parse(body);
    const json* d = Data(j);
    if (!d || !d->is_object()) return false;
    out->name = Str(*d, "instanceName");
    out->description = Str(*d, "instanceDescription");
    out->software = Str(*d, "softwareName");
    out->version = Str(*d, "softwareVersion");
    out->registration = Bool(*d, "registrationEnabled");
    out->maintenance = Bool(*d, "maintenanceModeEnabled");
    return true;
}

ServerInfo PingServer(const std::string& server) {
    ServerInfo info;
    const ULONGLONG t0 = GetTickCount64();
    const HttpReply r = HttpJson("GET", ApiBase(server) + "/instance", "", "");
    info.ms = static_cast<int>(GetTickCount64() - t0);
    if (r.status == 200 && ParseInstance(r.body, &info)) info.ok = true;
    else if (r.status == 200) info.error = "it answered, but not like a LittleBigPlanet server";
    else info.error = ExplainApiError(r.status, r.body, r.error);
    return info;
}

std::vector<Announcement> ParseAnnouncements(const std::string& body, std::string* error) {
    std::vector<Announcement> out;
    const json j = Parse(body);
    const json* d = Data(j);
    if (!d || !d->is_array()) {
        if (error) *error = "no announcements in the reply";
        return out;
    }
    for (const json& a : *d) {
        if (!a.is_object()) continue;
        Announcement n{Str(a, "title"), Str(a, "text"), Str(a, "createdAt")};
        if (n.created.size() > 10) n.created.resize(10);
        out.push_back(std::move(n));
    }
    return out;
}

std::vector<Announcement> FetchAnnouncements(const std::string& server, std::string* error) {
    const HttpReply r = HttpJson("GET", ApiBase(server) + "/announcements", "", "");
    if (r.status != 200) {
        if (error) *error = ExplainApiError(r.status, r.body, r.error);
        return {};
    }
    return ParseAnnouncements(r.body, error);
}

std::string LevelsPath(const std::string& category, const std::string& query, int skip, int count) {
    // skip is one-based on the server (it subtracts one).
    std::string p = "/levels/" + UrlEncode(category) + "?skip=" + std::to_string(skip + 1) + "&count=" + std::to_string(count);
    if (!query.empty()) p += "&query=" + UrlEncode(query);
    return p;
}

LevelPage ParseLevels(const std::string& body) {
    LevelPage page;
    const json j = Parse(body);
    const json* d = Data(j);
    if (!d || !d->is_array()) {
        page.error = "no levels in the reply";
        return page;
    }
    for (const json& l : *d) {
        if (!l.is_object()) continue;
        LevelCard c;
        c.id = Int(l, "levelId");
        c.title = Str(l, "title");
        c.description = Str(l, "description");
        c.icon_hash = Str(l, "iconHash");
        if (auto p = l.find("publisher"); p != l.end() && p->is_object()) c.publisher = Str(*p, "username");
        if (c.publisher.empty()) c.publisher = Str(l, "originalPublisher");
        c.hearts = Int(l, "hearts");
        c.plays = Int(l, "uniquePlays");
        c.yays = Int(l, "yayRatings");
        c.team_picked = Bool(l, "teamPicked");
        if (c.title.empty()) c.title = "Unnamed level";
        page.levels.push_back(std::move(c));
    }
    if (auto info = j.find("listInfo"); info != j.end() && info->is_object()) page.total = Int(*info, "totalItems");
    if (!page.total) page.total = static_cast<int>(page.levels.size());
    return page;
}

LevelPage FetchLevels(const std::string& server, const std::string& category, const std::string& query, int skip, int count) {
    const HttpReply r = HttpJson("GET", ApiBase(server) + LevelsPath(category, query, skip, count), "", "", 4u << 20);
    if (r.status != 200) {
        LevelPage p;
        p.error = ExplainApiError(r.status, r.body, r.error);
        return p;
    }
    return ParseLevels(r.body);
}

WebLogin RefreshLogin(const std::string& server, const std::string& email, const std::string& password) {
    WebLogin w;
    const std::string body = "{\"emailAddress\":\"" + JsonEscape(email) + "\",\"passwordSha512\":\"" + Sha512Hex(password) + "\"}";
    const HttpReply r = HttpJson("POST", ApiBase(server) + "/login", body, "");
    w.status = r.status;
    const json j = Parse(r.body);
    const json* d = Data(j);
    if (r.status == 200 && d && d->is_object() && !Str(*d, "tokenData").empty()) {
        w.ok = true;
        w.token = Str(*d, "tokenData");
    } else if (r.status == 200 && d && d->is_object() && !Str(*d, "resetToken").empty()) {
        w.error = "the server wants this account's password set again on its website first";
    } else {
        w.error = ExplainApiError(r.status, r.body, r.error);
    }
    return w;
}

bool QueueLevel(const std::string& server, const std::string& token, int id, std::string* error) {
    const HttpReply r = HttpJson("POST", ApiBase(server) + "/levels/id/" + std::to_string(id) + "/queue", "", token);
    if (r.status == 200) return true;
    if (error) *error = ExplainApiError(r.status, r.body, r.error);
    return false;
}

std::string FetchIcon(const std::string& server, const std::string& hash, std::string* error) {
    if (!IsSha1Hex(hash)) return {};
    const HttpReply r = HttpJson("GET", ApiBase(server) + "/assets/" + hash + "/image", "", "", 4u << 20);
    if (r.status == 200 && r.body.size() > 8 && r.body.compare(1, 3, "PNG") == 0) return r.body;
    if (error) *error = r.status == 200 ? "not a PNG" : ExplainApiError(r.status, r.body, r.error);
    return {};
}

}  // namespace launcher
