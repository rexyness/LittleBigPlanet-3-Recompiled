// See web_request.h. Ours.
#include "net/web_request.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

namespace net::web {
namespace {

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

HINTERNET Session() {
    static HINTERNET session = WinHttpOpen(L"LBP3-Recompiled", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    return session;
}

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::string LastError(const char* what) { return std::string(what) + " failed (" + std::to_string(GetLastError()) + ")"; }

}  // namespace

Response Request(const std::string& method, const std::string& url,
                 const std::vector<std::pair<std::string, std::string>>& headers, const std::string& body,
                 int timeoutMs) {
    Response r;
    HINTERNET session = Session();
    if (!session) {
        r.error = LastError("WinHttpOpen");
        return r;
    }
    const std::wstring wurl = Wide(url);
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256] = {}, path[4096] = {}, extra[4096] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = LastError("WinHttpCrackUrl");
        return r;
    }
    const bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    Handle con(WinHttpConnect(session, host, uc.nPort, 0));
    if (!con.h) {
        r.error = LastError("WinHttpConnect");
        return r;
    }
    const std::wstring object = std::wstring(path) + extra;
    Handle req(WinHttpOpenRequest(con.h, Wide(method).c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0));
    if (!req.h) {
        r.error = LastError("WinHttpOpenRequest");
        return r;
    }
    WinHttpSetTimeouts(req.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    std::wstring wh;
    for (const auto& [k, v] : headers) wh += Wide(k) + L": " + Wide(v) + L"\r\n";
    const BOOL sent = WinHttpSendRequest(req.h, wh.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wh.c_str(),
                                         wh.empty() ? 0 : static_cast<DWORD>(-1L),
                                         body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                                         static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(req.h, nullptr)) {
        r.error = LastError(sent ? "WinHttpReceiveResponse" : "WinHttpSendRequest");
        return r;
    }
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        const size_t at = r.body.size();
        r.body.resize(at + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, r.body.data() + at, avail, &got)) {
            r.body.resize(at);
            break;
        }
        r.body.resize(at + got);
        if (r.body.size() > 16u * 1024 * 1024) break;  // a runaway answer
    }
    r.ok = true;
    return r;
}

}  // namespace net::web
