// The on-screen keyboard's state, with no drawing and no input devices: which page is up,
// which key is selected, the text and its cursor. ui/osk_overlay.cpp draws it and feeds it
// the pad, the mouse and the physical keyboard; osktest drives it directly.
//
// The pages and keys are the PS4 keyboard's, as shadPS4 laid them out (ui/osk_tables.inc).
// Lengths are counted in UTF-16 units, as the game's buffer and maxTextLength count them.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ui::osk {

// Word suggestions (ui/osk_words.h) for the part of a word just before the cursor: given that
// part and the most UTF-16 units a suggestion may have (it replaces the part), the ones to
// show, best first.
using Suggester = std::function<std::vector<std::u16string>(std::u16string_view typed, size_t room)>;

enum class Action : uint8_t {
    None, Character, Shift, SymbolsMode, SpecialsMode, Space, Backspace,
    ArrowLeft, ArrowRight, ArrowUp, ArrowDown, Keyboard, Menu, Settings, NewLine, Done,
    PagePrev, PageNext,
};

enum class Glyph : uint8_t {
    None, Backspace, ArrowLeft, ArrowRight, ArrowUp, ArrowDown, ShiftOutline, ShiftFilled, CapsLockFilled,
};

struct KeySpec {
    uint8_t row = 0, col = 0, colSpan = 1, rowSpan = 1;
    const char* label = nullptr;   // UTF-8; what a Character key types
    const char* hotkey = nullptr;  // the pad shortcut hint: "L1", "R2", "TRI", "SQR", "L2+TRI"...
    Action action = Action::None;
    Glyph glyph = Glyph::None;
};

struct Layout {
    const KeySpec* keys = nullptr;
    size_t count = 0;
    uint8_t cols = 10, rows = 6;
    uint8_t functionRows = 2;  // the last rows: Shift/modes/Space/Backspace and arrows/Done
};

enum class Family : uint8_t { Latin, Symbols, Specials };
enum class Case : uint8_t { Lower, Upper, CapsLock };
enum class Outcome : uint8_t { None, Done };

// Keys that do something here. None is an empty slot; Keyboard (language switch), Menu and
// Settings (motion control) exist on a PS4 but have no function in this port. The rest
// are drawn and selectable.
bool Enabled(const KeySpec* key);

// The table for a page. Page counts: Latin 1, Symbols 2, Specials 2. CapsLock uses the
// Upper tables.
const Layout& LayoutFor(Family family, Case caseState, int page);
// The key covering a cell (spans included), or nullptr outside the grid.
const KeySpec* KeyAt(const Layout& layout, int row, int col);

// UTF-8 -> UTF-16 (labels, typed text) and back (logs).
std::u16string Utf16(std::string_view utf8);
std::string Utf8(std::u16string_view utf16);

class Keyboard {
public:
    // text: the prefill, clamped to maxLength. maxLength: the limit in UTF-16 units (>= 1).
    // The text cursor starts at the end, as on the PS4.
    Keyboard(std::u16string text, uint32_t maxLength, bool multiline = false);

    const Layout& layout() const { return LayoutFor(family_, case_, page_); }
    Family family() const { return family_; }
    Case caseState() const { return case_; }
    int page() const { return page_; }
    int row() const { return row_; }  // the logical cursor cell
    int col() const { return col_; }
    const KeySpec* selected() const;  // never null; always Enabled
    const std::u16string& text() const { return text_; }
    size_t cursor() const { return cursor_; }  // insertion point, 0..text().size()
    uint32_t maxLength() const { return maxLength_; }
    bool multiline() const { return multiline_; }

    // D-pad / stick: one step, skipping gaps and the rest of the key the cursor is on. A fresh
    // press wraps around the edges; a held (repeating) one stops at the edge. Up from the top
    // row goes into the prediction bar instead while it has suggestions (onto the first); in
    // the bar left/right move between them, down goes back to the key it came from.
    void Move(int dRow, int dCol, bool repeating = false);
    // Selects the key at a cell (mouse). False, and nothing changes, if that cell is a gap.
    bool Select(int row, int col);
    // Cross on the selected key, or on the selected suggestion (it's picked).
    Outcome Activate();

    // ---- Suggestions (none until a suggester is set) ----
    void SetSuggester(Suggester suggester);
    const std::vector<std::u16string>& suggestions() const { return suggestions_; }
    // The selected suggestion while the selection is in the prediction bar, else -1. While it
    // is, row() and col() still name the key it came from.
    int barIndex() const { return bar_; }
    bool inBar() const { return bar_ >= 0; }
    // Selects a suggestion (mouse hover). False if there's no such suggestion.
    bool SelectSuggestion(size_t index);
    // Picks a suggestion: it replaces the part of the word typed so far and is followed by a
    // space, unless one follows already (the cursor steps over it) or there's no room for it.
    // Then punctuation typed straight away goes before that space, and a space typed straight
    // away is swallowed. The selection goes back to the keys. False if there's no such one.
    bool Pick(size_t index);
    // Runs a key's action as if it had been pressed (Activate, and shortcuts).
    Outcome Press(const KeySpec& key);

    // Pad shortcuts and physical keys.
    void Backspace();    // Square, the Backspace key
    void Delete();       // the Delete key
    void Space();        // Triangle
    void NewLine();      // Enter when multiline
    void CursorLeft();   // L1, Left
    void CursorRight();  // R1, Right
    void CursorHome();   // Home; ArrowUp on a single line
    void CursorEnd();    // End; ArrowDown on a single line
    void CursorUp();     // ArrowUp: the previous line (multiline), else Home
    void CursorDown();   // ArrowDown: the next line (multiline), else End
    void Shift();        // L2: Lower -> Upper (one letter) -> CapsLock -> Lower
    void ToggleSymbols();   // L2+Triangle, the @#:/ABC key
    void ToggleSpecials();  // L3, the accents key
    // Inserts at the cursor if all of it fits in maxLength; false (nothing inserted) if not.
    // Line breaks are dropped unless multiline. A pending one-letter shift ends after it.
    bool Type(std::u16string_view s);

    // Counters for tests and the overlay (blink reset): bumped on every text or cursor change.
    uint64_t editCount() const { return edits_; }

private:
    void SetMode(Family family, int page, Action pressed);
    void FixSelection(Action pressed);
    bool Insert(std::u16string_view s, size_t cursorOffset);
    size_t LineStart(size_t pos) const;
    // After every change to the text or the cursor: the edit counter, the end of a pick's
    // space handling (unless `keepAutoSpace`), new suggestions.
    void Changed(bool keepAutoSpace = false);
    void Refresh();
    // Typing straight after a pick: punctuation goes before its space, a space is swallowed.
    // True if `s` was handled here.
    bool AfterPick(std::u16string_view s);

    std::u16string text_;
    size_t cursor_ = 0;
    uint32_t maxLength_ = 1;
    bool multiline_ = false;
    Family family_ = Family::Latin;
    Case case_ = Case::Lower;
    int page_ = 0;
    int row_ = 1, col_ = 0;  // starts on "q", as on the PS4
    uint64_t edits_ = 0;
    Suggester suggester_;
    std::vector<std::u16string> suggestions_;
    int bar_ = -1;
    bool autoSpace_ = false;  // the cursor is just past the space after a pick
};

// Held-button auto-repeat: the first press fires at once, then after delayMs every rateMs
// while held. Feed it every frame with the button state and a millisecond clock.
class Repeater {
public:
    Repeater(uint32_t delayMs = 400, uint32_t rateMs = 70) : delay_(delayMs), rate_(rateMs) {}
    // Returns how the button fired this frame: 0 = not, 1 = fresh press, 2 = a repeat.
    int Update(bool down, uint64_t nowMs);
    void Reset() { down_ = false; }

private:
    uint32_t delay_, rate_;
    bool down_ = false;
    uint64_t next_ = 0;
};

}  // namespace ui::osk
