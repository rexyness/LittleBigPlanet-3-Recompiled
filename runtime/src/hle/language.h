// The system language the game sees (hle/system_services.cpp, best-of-both-worlds branch).
//
// LBP3_LANGUAGE=<code or number> sets sceSystemServiceParamGetInt's language (default English,
// US). The US edition only accepts English and French: its language picker (eboot+0x2a05e0)
// keeps the system language only if the edition was sold with it, and falls back to English.
// Every edition carries all 22 languages' text, so LBP3_LANGUAGES=all (core/patches.cpp) makes
// the edition check at 0x2a0706 jump to the picker's "accepted" path (0x2a07ab). Those two
// addresses are LBPPC's finding (emurphy04, aad6a91, src/probe.c language_install); the patch
// here is our own, checked against the original bytes like every other.
#pragma once
#include <cstdint>

namespace hle::system {

// A code ("de", "fr", "pt-br", "en-gb", "es-la"...) or a number (0..29); -1 if unknown;
// nullptr or "" is English (US).
int32_t ParseLanguage(const char* value);
const char* LanguageName(int32_t id);
// LBP3_LANGUAGE, read once.
int32_t SystemLanguage();

}  // namespace hle::system
