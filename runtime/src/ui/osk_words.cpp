#include "ui/osk_words.h"

#include <algorithm>
#include <charconv>
#include <tuple>
#include <unordered_set>

#include "ui/osk_model.h"  // Utf16, Utf8

namespace ui::osk {
namespace {

bool IsJoiner(char16_t c) { return c == u'\'' || c == u'-' || c == 0x2019; }  // ' - and the curly '

// Glued to one of these, letters are a name tag or a code ("lvl2", "@sack", "my_level").
bool IsTagChar(char16_t c) { return (c >= u'0' && c <= u'9') || c == u'_' || c == u'@' || c == u'#'; }

// Letters on both sides of one of these are an address ("www.example.com", "a/b").
bool IsAddressChar(char16_t c) { return c == u'.' || c == u'/' || c == u':'; }

// Case pairs where the upper case is at an even code and the lower right after it.
bool EvenOddPair(char16_t c) {
    return (c >= 0x100 && c <= 0x12F) || (c >= 0x132 && c <= 0x137) || (c >= 0x14A && c <= 0x177) ||
           (c >= 0x218 && c <= 0x21B) || (c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) ||
           (c >= 0x4D0 && c <= 0x52F) || (c >= 0x1E00 && c <= 0x1E95) || (c >= 0x1EA0 && c <= 0x1EFF);
}
// ... and where it's at an odd code.
bool OddEvenPair(char16_t c) { return (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E) || (c >= 0x4C1 && c <= 0x4CE); }

std::string_view Trimmed(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return s;
}

template <typename Fn>
void ForEachLine(std::string_view text, Fn&& fn) {
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.remove_prefix(3);
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string_view::npos) nl = text.size();
        const std::string_view line = Trimmed(text.substr(at, nl - at));
        if (!line.empty() && line.front() != '#') fn(line);
        at = nl + 1;
    }
}

template <typename T>
bool ParseCount(std::string_view s, T& out) {
    if (s.empty()) return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc() && r.ptr == s.data() + s.size() && out > 0;
}

// One whole word in the list's sense: letters, ' or - between letters. `minLetters` = 2 for
// learned words; the list also has "I" and "a".
bool IsWord(std::u16string_view w, size_t minLetters) {
    if (w.empty() || !IsLetter(w.front()) || !IsLetter(w.back())) return false;
    size_t letters = 0;
    for (size_t i = 0; i < w.size(); ++i) {
        if (IsLetter(w[i])) {
            ++letters;
        } else if (!IsJoiner(w[i]) || !IsLetter(w[i - 1]) || !IsLetter(w[i + 1])) {
            return false;
        }
    }
    return letters >= minLetters;
}

// The curly apostrophe types as the straight one in what we store and compare.
std::u16string Key(std::u16string_view w) {
    std::u16string k = Lower(w);
    std::replace(k.begin(), k.end(), char16_t(0x2019), u'\'');
    return k;
}

}  // namespace

bool IsLetter(char16_t c) {
    if (c < 0x80) return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
    if (c >= 0xC0 && c <= 0x24F) return c != 0xD7 && c != 0xF7;
    if (c >= 0x386 && c <= 0x3FF) return c != 0x387;
    if (c >= 0x400 && c <= 0x52F) return c < 0x482 || c > 0x489;
    return c >= 0x1E00 && c <= 0x1EFF;
}

char16_t ToLower(char16_t c) {
    if (c < 0x80) return (c >= u'A' && c <= u'Z') ? char16_t(c + 32) : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return char16_t(c + 0x20);
    if (c == 0x130) return u'i';
    if (c == 0x178) return 0xFF;
    if (EvenOddPair(c)) return (c & 1) ? c : char16_t(c + 1);
    if (OddEvenPair(c)) return (c & 1) ? char16_t(c + 1) : c;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return char16_t(c + 0x20);
    if (c == 0x386) return 0x3AC;
    if (c >= 0x388 && c <= 0x38A) return char16_t(c + 0x25);
    if (c == 0x38C) return 0x3CC;
    if (c == 0x38E || c == 0x38F) return char16_t(c + 0x3F);
    if (c >= 0x410 && c <= 0x42F) return char16_t(c + 0x20);
    if (c >= 0x400 && c <= 0x40F) return char16_t(c + 0x50);
    if (c == 0x4C0) return 0x4CF;
    return c;
}

char16_t ToUpper(char16_t c) {
    if (c < 0x80) return (c >= u'a' && c <= u'z') ? char16_t(c - 32) : c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return char16_t(c - 0x20);
    if (c == 0xFF) return 0x178;
    if (c == 0x131) return u'I';
    if (EvenOddPair(c)) return (c & 1) ? char16_t(c - 1) : c;
    if (OddEvenPair(c)) return (c & 1) ? c : char16_t(c - 1);
    if (c >= 0x3B1 && c <= 0x3C9) return c == 0x3C2 ? char16_t(0x3A3) : char16_t(c - 0x20);
    if (c == 0x3AC) return 0x386;
    if (c >= 0x3AD && c <= 0x3AF) return char16_t(c - 0x25);
    if (c == 0x3CC) return 0x38C;
    if (c == 0x3CD || c == 0x3CE) return char16_t(c - 0x3F);
    if (c >= 0x430 && c <= 0x44F) return char16_t(c - 0x20);
    if (c >= 0x450 && c <= 0x45F) return char16_t(c - 0x50);
    if (c == 0x4CF) return 0x4C0;
    return c;
}

std::u16string Lower(std::u16string_view s) {
    std::u16string out(s);
    for (char16_t& c : out) c = ToLower(c);
    return out;
}

std::u16string Upper(std::u16string_view s) {
    std::u16string out(s);
    for (char16_t& c : out) c = ToUpper(c);
    return out;
}

std::vector<std::u16string> WordsIn(std::u16string_view text) {
    std::vector<std::u16string> out;
    size_t i = 0;
    while (i < text.size()) {
        if (!IsLetter(text[i])) {
            ++i;
            continue;
        }
        // A run of letters, with joiners between letters.
        const size_t start = i;
        size_t end = i;
        while (end < text.size() && (IsLetter(text[end]) || (IsJoiner(text[end]) && end + 1 < text.size() &&
                                                               IsLetter(text[end + 1]) && IsLetter(text[end - 1])))) {
            ++end;
        }
        bool glued = start > 0 && (IsTagChar(text[start - 1]) ||
                                   (IsAddressChar(text[start - 1]) && start >= 2 && IsLetter(text[start - 2])));
        if (end < text.size()) {
            const char16_t next = text[end];
            if (IsTagChar(next) || (IsAddressChar(next) && end + 1 < text.size() && IsLetter(text[end + 1]))) glued = true;
        }
        // A run glued to a code swallows the rest of the code too.
        if (glued) {
            while (end < text.size() && text[end] != u' ' && text[end] != u'\n' && text[end] != u'\t') ++end;
        } else {
            const std::u16string_view w = text.substr(start, end - start);
            if (IsWord(w, 2)) out.emplace_back(w);
        }
        i = end;
    }
    return out;
}

std::u16string_view TypedPart(std::u16string_view text, size_t cursor, size_t* start) {
    if (start) *start = cursor;
    cursor = std::min(cursor, text.size());
    if (cursor < text.size() && (IsLetter(text[cursor]) || IsJoiner(text[cursor]))) return {};
    size_t i = cursor;
    while (i > 0) {
        const char16_t c = text[i - 1];
        if (IsLetter(c)) {
            --i;
        } else if (IsJoiner(c) && i >= 2 && IsLetter(text[i - 2])) {
            --i;
        } else {
            break;
        }
    }
    if (i == cursor) return {};
    if (i > 0) {
        const char16_t before = text[i - 1];
        if (IsTagChar(before) || (IsAddressChar(before) && i >= 2 && IsLetter(text[i - 2]))) return {};
    }
    if (start) *start = i;
    return text.substr(i, cursor - i);
}

std::u16string FollowCase(std::u16string_view form, std::u16string_view typed) {
    size_t letters = 0, upper = 0;
    char16_t first = 0;
    for (char16_t c : typed) {
        if (!IsLetter(c)) continue;
        if (!first) first = c;
        ++letters;
        if (ToLower(c) != c) ++upper;
    }
    if (letters >= 2 && upper == letters) return Upper(form);
    std::u16string out(form);
    if (first && ToLower(first) != first && !out.empty()) out[0] = ToUpper(out[0]);
    return out;
}

// ---- Words ----

void Words::LoadList(std::string_view utf8) {
    list_.clear();
    listIndex_.clear();
    std::unordered_set<std::u16string> seen;
    ForEachLine(utf8, [&](std::string_view line) {
        const size_t tab = line.find('\t');
        if (tab == std::string_view::npos) return;
        uint64_t count = 0;
        if (!ParseCount(Trimmed(line.substr(tab + 1)), count)) return;
        const std::u16string form = Utf16(Trimmed(line.substr(0, tab)));
        if (!IsWord(form, 1)) return;
        std::u16string key = Key(form);
        if (!seen.insert(key).second) return;
        list_.push_back({std::move(key), form, count});
    });
    std::sort(list_.begin(), list_.end(), [](const ListWord& a, const ListWord& b) { return a.key < b.key; });
    for (size_t i = 0; i < list_.size(); ++i) listIndex_.emplace(list_[i].key, i);
    // A word learned before the list arrived takes the list's spelling.
    for (auto& [key, l] : learned_)
        if (const ListWord* w = FindListed(key)) l.form = w->form;
}

void Words::LoadLearned(std::string_view utf8) {
    learned_.clear();
    ForEachLine(utf8, [&](std::string_view line) {
        size_t sep = line.find_first_of("\t ");
        if (sep == std::string_view::npos) return;
        uint32_t count = 0;
        if (!ParseCount(line.substr(0, sep), count)) return;
        const std::u16string form = Utf16(Trimmed(line.substr(sep + 1)));
        if (!IsWord(form, 2)) return;
        const std::u16string key = Key(form);
        Learned& l = learned_[key];
        l.count = uint32_t(std::min<uint64_t>(uint64_t(l.count) + count, UINT32_MAX));
        const ListWord* w = FindListed(key);
        l.form = w ? w->form : form;
    });
    Trim();
}

std::string Words::SaveLearned() const {
    std::vector<std::pair<const std::u16string*, const Learned*>> rows;
    rows.reserve(learned_.size());
    for (const auto& [key, l] : learned_) rows.push_back({&key, &l});
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return std::tie(b.second->count, *a.first) < std::tie(a.second->count, *b.first);
    });
    std::string out =
        "# The words you've typed on the on-screen keyboard and how many times, most used first. They come\n"
        "# first in its suggestions; a word that isn't in the word list is suggested from its second use.\n"
        "# To forget a word, delete its line (with the game closed).\n";
    for (const auto& [key, l] : rows) {
        out += std::to_string(l->count);
        out += '\t';
        out += Utf8(l->form);
        out += '\n';
    }
    return out;
}

const Words::ListWord* Words::FindListed(const std::u16string& key) const {
    const auto it = listIndex_.find(key);
    return it == listIndex_.end() ? nullptr : &list_[it->second];
}

std::vector<std::u16string> Words::Suggest(std::u16string_view typed, size_t room, size_t max) const {
    std::vector<std::u16string> out;
    const std::u16string key = Key(typed);
    if (key.empty() || max == 0) return out;
    const auto startsWith = [&](const std::u16string& k) { return k.size() >= key.size() && k.compare(0, key.size(), key) == 0; };

    struct Candidate {
        int tier;            // 0 learned, 1 the list
        uint64_t used;       // learned count
        uint64_t common;     // the list's count
        const std::u16string* key;
        const std::u16string* form;
    };
    std::vector<Candidate> c;
    std::unordered_set<std::u16string_view> taken;
    for (const auto& [k, l] : learned_) {
        if (!startsWith(k)) continue;
        const ListWord* w = FindListed(k);
        if (!w && l.count < 2) continue;  // a new word only from its second use
        c.push_back({0, l.count, w ? w->count : 0, &k, w ? &w->form : &l.form});
        taken.insert(k);
    }
    auto it = std::lower_bound(list_.begin(), list_.end(), key, [](const ListWord& w, const std::u16string& k) { return w.key < k; });
    for (; it != list_.end() && startsWith(it->key); ++it)
        if (!taken.count(it->key)) c.push_back({1, 0, it->count, &it->key, &it->form});
    std::sort(c.begin(), c.end(), [](const Candidate& a, const Candidate& b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        if (a.used != b.used) return a.used > b.used;
        if (a.common != b.common) return a.common > b.common;
        return *a.key < *b.key;
    });
    for (const Candidate& k : c) {
        std::u16string shown = FollowCase(*k.form, typed);
        if (shown == typed || shown.size() > room) continue;
        if (std::find(out.begin(), out.end(), shown) != out.end()) continue;
        out.push_back(std::move(shown));
        if (out.size() == max) break;
    }
    return out;
}

bool Words::Learn(std::u16string_view opening, std::u16string_view confirmed) {
    std::unordered_map<std::u16string, int> before;
    for (const std::u16string& w : WordsIn(opening)) ++before[Key(w)];
    bool changed = false;
    for (const std::u16string& w : WordsIn(confirmed)) {
        const std::u16string key = Key(w);
        if (auto b = before.find(key); b != before.end() && b->second > 0) {
            --b->second;
            continue;
        }
        Learned& l = learned_[key];
        if (l.count < UINT32_MAX) ++l.count;
        const ListWord* listed = FindListed(key);
        l.form = listed ? listed->form : w;  // a new word: as typed this time
        changed = true;
    }
    Trim();
    return changed;
}

uint32_t Words::LearnedCount(std::u16string_view word) const {
    const auto it = learned_.find(Key(word));
    return it == learned_.end() ? 0 : it->second.count;
}

void Words::Trim() {
    if (learned_.size() <= kMaxLearned) return;
    std::vector<std::pair<uint32_t, std::u16string>> order;
    order.reserve(learned_.size());
    for (const auto& [key, l] : learned_) order.push_back({l.count, key});
    // The least used go first; among equals, the later in the alphabet.
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second > b.second;
    });
    for (size_t i = 0; learned_.size() > kMaxLearned; ++i) learned_.erase(order[i].second);
}

}  // namespace ui::osk
