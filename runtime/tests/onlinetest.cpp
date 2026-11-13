// Offline checks for online play (hle/online.h, hle/http_online.cpp): the login ticket's
// layout, the reply signing, and that online stays off without a server. No network.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "hle/online.h"

namespace hle::http_online {
std::vector<uint8_t> TestTicket(const std::string& name);
std::string TestHmac(const std::string& key, const std::string& data);
}  // namespace hle::http_online

static int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(c)) {                                                           \
            ++g_failed;                                                       \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #c);              \
        }                                                                     \
    } while (0)

static uint16_t Be16(const std::vector<uint8_t>& v, size_t at) { return uint16_t(v[at] << 8 | v[at + 1]); }

int main() {
    // Online needs LBP3_ONLINE=1 *and* a server; neither is set here.
    CHECK(!hle::online::Enabled());

    // RFC 2202 test case 2 for HMAC-SHA1.
    CHECK(hle::http_online::TestHmac("Jefe", "what do ya want for nothing?") ==
          "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");

    const auto t = hle::http_online::TestTicket("Sackboy");
    CHECK(t.size() > 16);
    CHECK(t[0] == 0x21 && t[1] == 0x01);                // ticket version 2.1
    CHECK(Be16(t, 6) == t.size() - 8);                  // sections length
    CHECK(t[8] == 0x30 && t[9] == 0x00);                // body section
    const size_t bodyLen = Be16(t, 10);
    CHECK(t[12 + bodyLen] == 0x30 && t[13 + bodyLen] == 0x02);  // footer section follows the body
    const std::string s(t.begin(), t.end());
    CHECK(s.find("Sackboy") != std::string::npos);
    CHECK(s.find("UP9000-BCUS98362_00") != std::string::npos);
    CHECK(s.find("LBPC") != std::string::npos);

    std::printf("onlinetest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
