// Offline checks for online play (hle/online.h, hle/http_online.cpp): the login ticket's
// layout, the reply signing, and that online stays off without a server. No network.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "hle/online.h"

namespace hle::http_online {
std::vector<uint8_t> TestTicket(const std::string& name, const std::string& code = {});
std::string TestHmac(const std::string& key, const std::string& data);
int TestDownloadCheck(const std::string& path, std::vector<uint8_t>& body);
}  // namespace hle::http_online

// The download check: the status the game gets for a GET .../r/<sha1> answered 200 with these
// bytes, and whether the body reached it unchanged.
static bool Passes(const std::string& path, const std::string& bytes) {
    std::vector<uint8_t> body(bytes.begin(), bytes.end());
    const std::vector<uint8_t> before = body;
    return hle::http_online::TestDownloadCheck(path, body) == 200 && body == before;
}
static bool Refused(const std::string& path, const std::string& bytes) {
    std::vector<uint8_t> body(bytes.begin(), bytes.end());
    return hle::http_online::TestDownloadCheck(path, body) == 404 && body.empty();
}

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

    // The serial: the body's first field (type 8, 0x14 bytes, NUL-padded). Without a sign-in code
    // the old one; with one, "lbppc-code:" + the code (LBPPC 0.1.3), the length unchanged.
    const auto serialOf = [](const std::vector<uint8_t>& v) {
        std::string out;
        if (v.size() < 16 + 0x14 || Be16(v, 12) != 8 || Be16(v, 14) != 0x14) return std::string("<bad field>");
        for (size_t i = 16; i < 16 + 0x14 && v[i]; ++i) out += char(v[i]);
        return out;
    };
    CHECK(serialOf(t) == "lbp3-local-ticket");
    const auto tc = hle::http_online::TestTicket("Sackboy", "AB12CD");
    CHECK(serialOf(tc) == "lbppc-code:AB12CD");
    CHECK(tc.size() == t.size());                      // same layout, only the serial's bytes differ
    CHECK(tc[16 + 17] == 0 && tc[16 + 0x13] == 0);     // NUL-padded to 0x14
    size_t differ = 0;
    for (size_t i = 0; i < t.size(); ++i) differ += t[i] != tc[i] && (i < 16 || i >= 16 + 0x14) ? 1 : 0;
    // Outside the serial only the issued/expires times may differ (two calls, a millisecond apart).
    CHECK(differ <= 16);
    CHECK(std::string(tc.begin(), tc.end()).find("Sackboy") != std::string::npos);

    // Downloads (GET .../r/<sha1>): the bytes must hash to their name and be a shared type.
    const std::string r = "/LITTLEBIGPLANETPS3_XML/r/";
    const std::string level("LVLb\x00\x00\x00\x01hello level", 19);
    const std::string photo("\xff\xd8\xff\xe0JFIF photo", 14);
    const std::string script("FSHb\x00\x00\x01script", 13);
    const std::string shader("GMTb\x00\x00\x01shader", 13);
    const std::string odd("LVLx\x00\x00\x00\x01odd", 11);
    CHECK(Passes(r + "4a3f9c5934a644b000c4ec272b537b106c4806ea", level));
    CHECK(Passes(r + "4A3F9C5934A644B000C4EC272B537B106C4806EA", level));             // upper-case name
    CHECK(Passes(r + "4a3f9c5934a644b000c4ec272b537b106c4806ea?x=1", level));         // with a query
    CHECK(Passes(r + "000772537167906051b14cabb0fb9952a6616bef", photo));             // a photo
    CHECK(Refused(r + "caaa190759bbf59e3b1704f7ef3e607cd966b09c", level));            // another file's name
    CHECK(Refused(r + "4b1e238161eff5bedd129f1627c2aa466618bf29", script));           // a script
    CHECK(Refused(r + "8932ee0179133e11ba9db3b965f5bd6accdda0bd", shader));           // shaders
    CHECK(Refused(r + "da20068077fa54bb93be815d5371f172f23ee221", odd));              // unknown format
    CHECK(Passes("/LITTLEBIGPLANETPS3_XML/announce", script));                       // not a download
    CHECK(Passes(r + "4a3f9c5934a644b000c4ec272b537b106c4806eaX", script));          // not a hash name

    std::printf("onlinetest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
