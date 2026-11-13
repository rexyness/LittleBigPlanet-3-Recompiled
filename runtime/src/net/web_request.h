// A plain HTTP(S) request over WinHTTP, for shadNet's Web API (hle/np_shadnet.cpp). Separate from the
// game-server client (hle/http_online.cpp), which adds that server's cookie and signing fix-ups.
// Blocking, with timeouts; HTTPS uses Windows' certificate store.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace net::web {

struct Response {
    bool ok = false;     // an HTTP answer came back (any status)
    int status = 0;      // its status code
    std::string body;    // its body
    std::string error;   // when !ok: what failed (for the log)
};

// method: "GET", "POST", "PUT", "DELETE", "PATCH". url: "http(s)://host[:port]/path?query".
Response Request(const std::string& method, const std::string& url,
                 const std::vector<std::pair<std::string, std::string>>& headers, const std::string& body,
                 int timeoutMs = 10000);

}  // namespace net::web
