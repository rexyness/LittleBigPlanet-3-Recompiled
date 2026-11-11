#include "ui/osk_model.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace ui::osk {
namespace {

#include "ui/osk_tables.inc"

template <size_t N>
constexpr Layout MakeLayout(const std::array<KeySpec, N>& keys, uint8_t rows) {
    return Layout{keys.data(), N, 10, rows, 2};
}

constexpr Layout kLatinLower = MakeLayout(kLatinLowerKeys, 6);
constexpr Layout kLatinUpper = MakeLayout(kLatinUpperKeys, 6);
constexpr Layout kSymbols1 = MakeLayout(kSymbolsPage1Keys, 6);
constexpr Layout kSymbols2 = MakeLayout(kSymbolsPage2Keys, 6);
constexpr Layout kSpecials1 = MakeLayout(kSpecialsPage1Keys, 7);
constexpr Layout kSpecials1Upper = MakeLayout(kSpecialsPage1UpperKeys, 7);
constexpr Layout kSpecials2 = MakeLayout(kSpecialsPage2Keys, 7);
constexpr Layout kSpecials2Upper = MakeLayout(kSpecialsPage2UpperKeys, 7);

bool IsHigh(char16_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLow(char16_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// Keys whose cursor position should survive a page switch as each other.
Action PairOf(Action a) {
    switch (a) {
    case Action::PageNext: return Action::PagePrev;
    case Action::PagePrev: return Action::PageNext;
    default: return a;
    }
}

}  // namespace

bool Enabled(const KeySpec* key) {
    if (!key) return false;
    switch (key->action) {
    case Action::None:
    case Action::Keyboard:
    case Action::Menu:
    case Action::Settings:
        return false;
    default:
        return true;
    }
}

const Layout& LayoutFor(Family family, Case caseState, int page) {
    const bool upper = caseState != Case::Lower;
    const bool second = (page & 1) != 0;
    switch (family) {
    case Family::Symbols: return second ? kSymbols2 : kSymbols1;
    case Family::Specials:
        if (second) return upper ? kSpecials2Upper : kSpecials2;
        return upper ? kSpecials1Upper : kSpecials1;
    case Family::Latin:
    default:
        return upper ? kLatinUpper : kLatinLower;
    }
}

const KeySpec* KeyAt(const Layout& layout, int row, int col) {
    if (row < 0 || col < 0 || row >= layout.rows || col >= layout.cols) return nullptr;
    const KeySpec* found = nullptr;
    for (size_t i = 0; i < layout.count; ++i) {
        const KeySpec& k = layout.keys[i];
        const int r0 = k.row, c0 = k.col;
        const int r1 = r0 + std::max<int>(1, k.rowSpan), c1 = c0 + std::max<int>(1, k.colSpan);
        if (row >= r0 && row < r1 && col >= c0 && col < c1) found = &k;  // later keys win, as in shadPS4
    }
    return found;
}

std::u16string Utf16(std::string_view s) {
    std::u16string out;
    size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        size_t n = 1;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0) {
            n = 2;
        } else if ((b & 0xF0) == 0xE0) {
            n = 3;
        } else if ((b & 0xF8) == 0xF0) {
            n = 4;
        }
        if (n > 1) {
            if (i + n > s.size()) {
                n = 1;  // truncated sequence: one replacement character, then resync
            } else {
                cp = b & (0x7F >> n);
                bool ok = true;
                for (size_t k = 1; k < n; ++k) {
                    const auto c = static_cast<unsigned char>(s[i + k]);
                    if ((c & 0xC0) != 0x80) {
                        ok = false;
                        break;
                    }
                    cp = (cp << 6) | (c & 0x3F);
                }
                if (!ok) {
                    cp = 0xFFFD;
                    n = 1;
                }
            }
        }
        i += n;
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp > 0x10FFFF ? 0xFFFD : cp));
        }
    }
    return out;
}

std::string Utf8(std::u16string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char32_t cp = s[i];
        if (IsHigh(s[i]) && i + 1 < s.size() && IsLow(s[i + 1])) {
            cp = 0x10000 + ((char32_t(s[i]) - 0xD800) << 10) + (char32_t(s[i + 1]) - 0xDC00);
            ++i;
        } else if (IsHigh(s[i]) || IsLow(s[i])) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// ---- Keyboard ----

Keyboard::Keyboard(std::u16string text, uint32_t maxLength, bool multiline)
    : text_(std::move(text)), maxLength_(std::max<uint32_t>(1, maxLength)), multiline_(multiline) {
    if (text_.size() > maxLength_) {
        size_t n = maxLength_;
        if (IsHigh(text_[n - 1])) --n;  // don't keep half a pair
        text_.resize(n);
    }
    cursor_ = text_.size();
}

const KeySpec* Keyboard::selected() const { return KeyAt(layout(), row_, col_); }

void Keyboard::Move(int dRow, int dCol, bool repeating) {
    const Layout& l = layout();
    const KeySpec* origin = selected();
    if (dCol != 0 && dRow == 0) {
        // Across a row: cell by cell, past gaps and the rest of a wide key.
        int c = col_;
        for (int i = 0; i < l.cols; ++i) {
            int next = c + (dCol > 0 ? 1 : -1);
            if (next < 0 || next >= l.cols) {
                if (repeating) return;  // a held press stops at the edge
                next = (next + l.cols) % l.cols;
            }
            c = next;
            const KeySpec* k = KeyAt(l, row_, c);
            if (!Enabled(k) || k == origin) continue;
            col_ = c;
            return;
        }
        return;
    }
    if (dRow == 0) return;
    // Up or down: the nearest key in the next row that has one (a gap in the column doesn't
    // send the cursor around the whole keyboard). A key spanning rows (the symbol pages' page
    // key) is left from its first row going up and its last row going down.
    int r = row_;
    if (origin) r = dRow > 0 ? origin->row + std::max<int>(1, origin->rowSpan) - 1 : origin->row;
    for (int i = 0; i < l.rows; ++i) {
        int next = r + (dRow > 0 ? 1 : -1);
        if (next < 0 || next >= l.rows) {
            if (repeating) return;
            next = (next + l.rows) % l.rows;
        }
        r = next;
        int bestCol = -1, bestDist = 1 << 30;
        for (int c = 0; c < l.cols; ++c) {
            const KeySpec* k = KeyAt(l, r, c);
            if (!Enabled(k) || k == origin) continue;
            const int dist = std::abs(c - col_);
            if (dist < bestDist) {  // ties keep the left one
                bestDist = dist;
                bestCol = c;
            }
        }
        if (bestCol < 0) continue;
        row_ = r;
        if (bestDist != 0) col_ = bestCol;  // the logical column survives when the cell has a key
        return;
    }
}

bool Keyboard::Select(int row, int col) {
    if (!Enabled(KeyAt(layout(), row, col))) return false;
    row_ = row;
    col_ = col;
    return true;
}

Outcome Keyboard::Activate() {
    const KeySpec* key = selected();
    if (!key) return Outcome::None;
    const KeySpec copy = *key;
    const Action before = copy.action;
    const Outcome out = Press(copy);
    // A mode or page key keeps the cursor on itself (or its partner) on the new page.
    if (before == Action::SymbolsMode || before == Action::SpecialsMode || before == Action::PageNext ||
        before == Action::PagePrev) {
        FixSelection(before);
    }
    return out;
}

Outcome Keyboard::Press(const KeySpec& key) {
    switch (key.action) {
    case Action::Character: {
        const std::u16string s = Utf16(key.label ? key.label : "");
        const bool pair = s == u"()" || s == u"[]" || s == u"{}";
        if (Insert(s, pair ? 1 : s.size()) && case_ == Case::Upper) case_ = Case::Lower;
        return Outcome::None;
    }
    case Action::Shift: Shift(); return Outcome::None;
    case Action::SymbolsMode: ToggleSymbols(); return Outcome::None;
    case Action::SpecialsMode: ToggleSpecials(); return Outcome::None;
    case Action::Space: Space(); return Outcome::None;
    case Action::Backspace: Backspace(); return Outcome::None;
    case Action::ArrowLeft: CursorLeft(); return Outcome::None;
    case Action::ArrowRight: CursorRight(); return Outcome::None;
    case Action::ArrowUp: CursorUp(); return Outcome::None;
    case Action::ArrowDown: CursorDown(); return Outcome::None;
    case Action::NewLine: NewLine(); return Outcome::None;
    case Action::Done: return Outcome::Done;
    case Action::PageNext:
    case Action::PagePrev: SetMode(family_, page_ ^ 1, Action::None); return Outcome::None;
    default: return Outcome::None;
    }
}

bool Keyboard::Insert(std::u16string_view s, size_t cursorOffset) {
    if (s.empty()) return true;
    if (text_.size() + s.size() > maxLength_) return false;
    text_.insert(cursor_, s);
    cursor_ += std::min(cursorOffset, s.size());
    ++edits_;
    return true;
}

bool Keyboard::Type(std::u16string_view s) {
    std::u16string clean;
    for (char16_t c : s) {
        if (c == u'\r') continue;
        if (c == u'\n' && !multiline_) continue;
        if (c < 0x20 && c != u'\n') continue;
        clean.push_back(c);
    }
    if (clean.empty()) return true;
    if (!Insert(clean, clean.size())) return false;
    if (case_ == Case::Upper) case_ = Case::Lower;
    return true;
}

void Keyboard::Backspace() {
    if (cursor_ == 0) return;
    size_t n = 1;
    if (cursor_ >= 2 && IsLow(text_[cursor_ - 1]) && IsHigh(text_[cursor_ - 2])) n = 2;
    text_.erase(cursor_ - n, n);
    cursor_ -= n;
    ++edits_;
}

void Keyboard::Delete() {
    if (cursor_ >= text_.size()) return;
    size_t n = 1;
    if (cursor_ + 1 < text_.size() && IsHigh(text_[cursor_]) && IsLow(text_[cursor_ + 1])) n = 2;
    text_.erase(cursor_, n);
    ++edits_;
}

void Keyboard::Space() { Insert(u" ", 1); }

void Keyboard::NewLine() {
    if (multiline_) Insert(u"\n", 1);
}

void Keyboard::CursorLeft() {
    if (cursor_ == 0) return;
    --cursor_;
    if (cursor_ > 0 && IsLow(text_[cursor_]) && IsHigh(text_[cursor_ - 1])) --cursor_;
    ++edits_;
}

void Keyboard::CursorRight() {
    if (cursor_ >= text_.size()) return;
    ++cursor_;
    if (cursor_ < text_.size() && IsLow(text_[cursor_]) && IsHigh(text_[cursor_ - 1])) ++cursor_;
    ++edits_;
}

void Keyboard::CursorHome() {
    const size_t to = multiline_ ? LineStart(cursor_) : 0;
    if (cursor_ != to) {
        cursor_ = to;
        ++edits_;
    }
}

void Keyboard::CursorEnd() {
    size_t to = text_.size();
    if (multiline_) {
        const size_t nl = text_.find(u'\n', cursor_);
        if (nl != std::u16string::npos) to = nl;
    }
    if (cursor_ != to) {
        cursor_ = to;
        ++edits_;
    }
}

size_t Keyboard::LineStart(size_t pos) const {
    while (pos > 0 && text_[pos - 1] != u'\n') --pos;
    return pos;
}

void Keyboard::CursorUp() {
    if (!multiline_) {
        CursorHome();
        return;
    }
    const size_t start = LineStart(cursor_);
    if (start == 0) {
        CursorHome();
        return;
    }
    const size_t column = cursor_ - start;
    const size_t prevStart = LineStart(start - 1);
    const size_t prevLen = (start - 1) - prevStart;
    cursor_ = prevStart + std::min(column, prevLen);
    if (cursor_ > 0 && cursor_ < text_.size() && IsHigh(text_[cursor_ - 1]) && IsLow(text_[cursor_])) --cursor_;
    ++edits_;
}

void Keyboard::CursorDown() {
    if (!multiline_) {
        CursorEnd();
        return;
    }
    const size_t start = LineStart(cursor_);
    const size_t nl = text_.find(u'\n', cursor_);
    if (nl == std::u16string::npos) {
        CursorEnd();
        return;
    }
    const size_t column = cursor_ - start;
    const size_t nextStart = nl + 1;
    size_t nextEnd = text_.find(u'\n', nextStart);
    if (nextEnd == std::u16string::npos) nextEnd = text_.size();
    cursor_ = nextStart + std::min(column, nextEnd - nextStart);
    if (cursor_ > 0 && cursor_ < text_.size() && IsHigh(text_[cursor_ - 1]) && IsLow(text_[cursor_])) --cursor_;
    ++edits_;
}

void Keyboard::Shift() {
    if (family_ == Family::Symbols) return;  // the symbol pages have no case
    switch (case_) {
    case Case::Lower: case_ = Case::Upper; break;
    case Case::Upper: case_ = Case::CapsLock; break;
    case Case::CapsLock: case_ = Case::Lower; break;
    }
}

void Keyboard::ToggleSymbols() {
    SetMode(family_ == Family::Symbols ? Family::Latin : Family::Symbols, 0, Action::None);
}

void Keyboard::ToggleSpecials() {
    SetMode(family_ == Family::Specials ? Family::Latin : Family::Specials, 0, Action::None);
}

void Keyboard::SetMode(Family family, int page, Action pressed) {
    const int oldRows = layout().rows;
    const int oldTyping = oldRows - layout().functionRows;
    family_ = family;
    page_ = page;
    const Layout& l = layout();
    const int newTyping = l.rows - l.functionRows;
    // The function rows sit at the bottom of every page: keep the cursor in the same one.
    if (row_ >= oldTyping) {
        row_ = newTyping + (row_ - oldTyping);
    } else {
        row_ = std::min(row_, newTyping - 1);
    }
    row_ = std::clamp(row_, 0, l.rows - 1);
    col_ = std::clamp(col_, 0, l.cols - 1);
    FixSelection(pressed);
}

void Keyboard::FixSelection(Action pressed) {
    const Layout& l = layout();
    if (pressed != Action::None) {
        const KeySpec* k = KeyAt(l, row_, col_);
        if (!k || (k->action != pressed && k->action != PairOf(pressed))) {
            for (Action want : {pressed, PairOf(pressed)}) {
                for (size_t i = 0; i < l.count; ++i) {
                    if (l.keys[i].action != want) continue;
                    row_ = l.keys[i].row;
                    col_ = l.keys[i].col;
                    return;
                }
            }
        }
    }
    if (Enabled(KeyAt(l, row_, col_))) return;
    // The nearest key: the same row first (closest column, left on a tie), then outwards.
    for (int dr = 0; dr < l.rows; ++dr) {
        for (int sign : {-1, 1}) {
            const int r = row_ + sign * dr;
            if (r < 0 || r >= l.rows || (dr == 0 && sign == 1)) continue;
            for (int dc = 0; dc < l.cols; ++dc) {
                for (int s2 : {-1, 1}) {
                    const int c = col_ + s2 * dc;
                    if (c < 0 || c >= l.cols || (dc == 0 && s2 == 1)) continue;
                    if (Enabled(KeyAt(l, r, c))) {
                        row_ = r;
                        col_ = c;
                        return;
                    }
                }
            }
        }
    }
}

// ---- Repeater ----

int Repeater::Update(bool down, uint64_t nowMs) {
    if (!down) {
        down_ = false;
        return 0;
    }
    if (!down_) {
        down_ = true;
        next_ = nowMs + delay_;
        return 1;
    }
    if (nowMs >= next_) {
        next_ = nowMs + rate_;
        return 2;
    }
    return 0;
}

}  // namespace ui::osk
