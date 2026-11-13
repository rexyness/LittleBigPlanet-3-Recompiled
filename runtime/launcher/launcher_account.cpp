// LBP3Launcher's "Create account" (launcher_account.h).
#include "launcher_account.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace launcher {
namespace {

std::wstring W(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::string WinError(const std::string& what) {
    return what + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

}  // namespace

HttpReply HttpJson(const std::string& method, const std::string& url, const std::string& body,
                   const std::string& authorization) {
    HttpReply r;
    std::wstring wurl = W(url);
    URL_COMPONENTS uc{sizeof uc};
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "not a URL: " + url;
        return r;
    }
    Handle session{WinHttpOpen(L"LBP3Launcher/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return r.error = WinError("WinHttpOpen"), r;
    WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 15000);
    Handle connect{WinHttpConnect(session.h, host, uc.nPort, 0)};
    if (!connect.h) return r.error = WinError("connecting"), r;
    const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request{WinHttpOpenRequest(connect.h, W(method).c_str(), path, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!request.h) return r.error = WinError("WinHttpOpenRequest"), r;
    std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    if (!authorization.empty()) headers += L"Authorization: " + W(authorization) + L"\r\n";
    if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(-1),
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = WinError("the request to " + url);
        return r;
    }
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.h, chunk.data(), avail, &read) || read == 0) break;
        r.body.append(chunk.data(), read);
        if (r.body.size() > (1u << 20)) break;  // an account reply is small
    }
    return r;
}

std::string Sha512Hex(const std::string& text) {
    unsigned char hash[64] = {};
    BCryptHash(BCRYPT_SHA512_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
               static_cast<ULONG>(text.size()), hash, sizeof hash);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char b : hash) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 15]);
    }
    return out;
}

std::string RandomPassword(size_t length) {
    static const char set[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";  // no look-alikes
    const size_t n = sizeof set - 1;
    std::string out;
    while (out.size() < length) {
        unsigned char buf[64];
        BCryptGenRandom(nullptr, buf, sizeof buf, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        for (unsigned char b : buf) {
            if (b >= 256 - (256 % n)) continue;  // no modulo bias
            out.push_back(set[b % n]);
            if (out.size() == length) break;
        }
    }
    return out;
}

std::string JsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                o += buf;
            } else {
                o.push_back(c);
            }
        }
    }
    return o;
}

namespace {
// Position just after `"key"` followed by optional blanks and ':' and blanks; npos if absent.
size_t AfterKey(const std::string& json, const std::string& key) {
    const std::string quoted = "\"" + key + "\"";
    for (size_t at = json.find(quoted); at != std::string::npos; at = json.find(quoted, at + 1)) {
        size_t i = at + quoted.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
        if (i < json.size() && json[i] == ':') {
            ++i;
            while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
            return i;
        }
    }
    return std::string::npos;
}
}  // namespace

std::string JsonString(const std::string& json, const std::string& key) {
    size_t i = AfterKey(json, key);
    if (i == std::string::npos || i >= json.size() || json[i] != '"') return {};
    std::string out;
    for (++i; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            const char e = json[++i];
            switch (e) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'u':
                if (i + 4 < json.size()) {
                    const unsigned cp = static_cast<unsigned>(std::strtoul(json.substr(i + 1, 4).c_str(), nullptr, 16));
                    if (cp < 0x80) out.push_back(static_cast<char>(cp));
                    else out.push_back('?');
                    i += 4;
                }
                break;
            default: out.push_back(e); break;
            }
        } else {
            out.push_back(json[i]);
        }
    }
    return out;
}

bool JsonTrue(const std::string& json, const std::string& key) {
    const size_t i = AfterKey(json, key);
    return i != std::string::npos && json.compare(i, 4, "true") == 0;
}

AccountFile ParseAccountFile(const std::string& text) {
    AccountFile a;
    auto trim = [](std::string v) {
        while (!v.empty() && static_cast<unsigned char>(v.back()) <= ' ') v.pop_back();
        size_t i = 0;
        while (i < v.size() && static_cast<unsigned char>(v[i]) <= ' ') ++i;
        return v.substr(i);
    };
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(at, nl - at);
        at = nl + 1;
        size_t sep = line.find('=');
        if (sep == std::string::npos) sep = line.find(':');
        if (sep == std::string::npos) continue;
        std::string key = trim(line.substr(0, sep));
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string value = trim(line.substr(sep + 1));
        if (value.empty() || (!key.empty() && key[0] == '#')) continue;
        if (key == "name" || key == "online_name" || key == "online name" || key == "onlinename" || key == "username" || key == "npid")
            a.name = value;
        else if (key == "password" || key == "pass" || key == "shadnet_password")
            a.password = value;
        else if (key == "server")
            a.server = value;
    }
    return a;
}

AccountResult CreateRefreshAccount(const std::string& server, const std::string& name) {
    AccountResult r;
    const std::string api = server + "/api/v3";
    r.password = RandomPassword();
    const std::string email = name + "@lbppc.invalid";  // the servers send no mail; one address per name
    const std::string body = "{\"username\":\"" + JsonEscape(name) + "\",\"emailAddress\":\"" + JsonEscape(email) +
                             "\",\"passwordSha512\":\"" + Sha512Hex(r.password) + "\"}";
    const HttpReply reg = HttpJson("POST", api + "/register", body, "");
    if (reg.status == 0) {
        r.message = "Couldn't reach " + server + ": " + reg.error;
        return r;
    }
    if (!JsonTrue(reg.body, "success")) {
        const std::string why = JsonString(reg.body, "error");
        r.message = "The server refused: " + (why.empty() ? "HTTP " + std::to_string(reg.status) : why);
        return r;
    }
    const std::string token = JsonString(reg.body, "tokenData");
    if (token.empty()) {
        r.message = "Registered, but the server sent no sign-in token; log in on its website to finish.";
        return r;
    }
    const HttpReply allow = HttpJson("PATCH", api + "/users/me",
                                     "{\"psnAuthenticationAllowed\":true,\"rpcnAuthenticationAllowed\":true}", token);
    if (allow.status == 0 || !JsonTrue(allow.body, "success")) {
        const std::string why = allow.status == 0 ? allow.error : JsonString(allow.body, "error");
        r.ok = true;  // the account exists; the password is still needed
        r.message = "Account created, but game logins aren't allowed yet (" +
                    (why.empty() ? "HTTP " + std::to_string(allow.status) : why) +
                    "). Turn on PSN/RPCN logins in your profile on the server's website.";
        return r;
    }
    r.ok = true;
    r.message = "Account \"" + name + "\" created on " + server + ".";
    return r;
}

}  // namespace launcher
