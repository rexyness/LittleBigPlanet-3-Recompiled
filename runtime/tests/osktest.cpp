// Offline checks for the on-screen keyboard (ui/osk_model, ui/osk_overlay). Prints each
// failed check; exit code 0 = all passed.
//
//   osktest               the model (pages, moves, shift, limits, editing), then the overlay
//                         in a hidden window driven by a scripted pad, keys and a fake clock
//   osktest --shot <dir>  PNGs of every page over a fake game frame, at 1280x720 and
//                         1920x1080, rendered offscreen (no window is shown)
//   osktest --manual      opens a window (on the display LBP3_DISPLAY names, e.g. 165hz)
//                         with the keyboard up, for a person to try it with a pad; Ctrl+C or
//                         closing the window ends it
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <windows.h>

#include "hle/pad_types.h"
#include "input/gamepad.h"
#include "ui/osk_model.h"
#include "ui/osk_overlay.h"
#include "ui/osk_testing.h"
#include "ui/osk_words.h"

using namespace ui::osk;

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL (line %d): %s\n", line, what);
    }
}
#define CHECK(c) Check((c), #c, __LINE__)
#define CHECK_EQ(a, b) Check((a) == (b), #a " == " #b, __LINE__)

std::string Label(const Keyboard& kb) {
    const KeySpec* k = kb.selected();
    return k && k->label ? k->label : "";
}
Action SelAction(const Keyboard& kb) { return kb.selected()->action; }

const KeySpec* Find(const Layout& l, Action a) {
    for (size_t i = 0; i < l.count; ++i)
        if (l.keys[i].action == a) return &l.keys[i];
    return nullptr;
}

// ---- Model ----

void TablesChecks() {
    std::printf("tables\n");
    struct Page {
        Family f;
        Case c;
        int page;
        int rows;
    };
    const Page pages[] = {{Family::Latin, Case::Lower, 0, 6},   {Family::Latin, Case::Upper, 0, 6},
                          {Family::Latin, Case::CapsLock, 0, 6}, {Family::Symbols, Case::Lower, 0, 6},
                          {Family::Symbols, Case::Lower, 1, 6},  {Family::Specials, Case::Lower, 0, 7},
                          {Family::Specials, Case::Upper, 0, 7}, {Family::Specials, Case::Lower, 1, 7},
                          {Family::Specials, Case::Upper, 1, 7}};
    for (const Page& p : pages) {
        const Layout& l = LayoutFor(p.f, p.c, p.page);
        CHECK_EQ(int(l.rows), p.rows);
        CHECK_EQ(int(l.cols), 10);
        CHECK(Find(l, Action::Done) != nullptr);
        CHECK(Find(l, Action::Backspace) != nullptr);
        CHECK(Find(l, Action::Space) != nullptr);
        CHECK(Find(l, Action::SymbolsMode) != nullptr);
        bool ok = true;
        for (size_t i = 0; i < l.count; ++i) {
            const KeySpec& k = l.keys[i];
            if (k.row + k.rowSpan > l.rows || k.col + k.colSpan > l.cols) ok = false;
            if (k.action == Action::Character && (!k.label || !*k.label)) ok = false;
            if (Enabled(&k) && KeyAt(l, k.row, k.col) != &k) ok = false;  // each drawn key owns its cell
        }
        CHECK(ok);
        // From every key, a step in any direction lands on a key that is drawn.
        bool moves = true;
        for (size_t i = 0; i < l.count; ++i) {
            if (!Enabled(&l.keys[i])) continue;
            for (int d = 0; d < 4; ++d) {
                Keyboard kb(u"", 10);
                if (p.f == Family::Symbols) kb.ToggleSymbols();
                if (p.f == Family::Specials) kb.ToggleSpecials();
                if (p.page) kb.Press(KeySpec{0, 0, 1, 1, nullptr, nullptr, Action::PageNext});
                if (p.c != Case::Lower) kb.Shift();
                if (p.c == Case::CapsLock) kb.Shift();
                if (&kb.layout() != &l || !kb.Select(l.keys[i].row, l.keys[i].col)) {
                    moves = false;
                    continue;
                }
                const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
                kb.Move(dr[d], dc[d], false);
                if (!Enabled(kb.selected())) moves = false;
                kb.Move(dr[d], dc[d], true);
                if (!Enabled(kb.selected())) moves = false;
            }
        }
        CHECK(moves);
    }
    // The keys the PS4 keyboard shows, with their pad hints.
    const Layout& l = LayoutFor(Family::Latin, Case::Lower, 0);
    CHECK_EQ(std::string(KeyAt(l, 1, 0)->label), std::string("q"));
    CHECK_EQ(std::string(KeyAt(l, 0, 9)->label), std::string("0"));
    CHECK_EQ(std::string(Find(l, Action::Shift)->hotkey), std::string("L2"));
    CHECK_EQ(std::string(Find(l, Action::Space)->hotkey), std::string("TRI"));
    CHECK_EQ(std::string(Find(l, Action::Backspace)->hotkey), std::string("SQR"));
    CHECK_EQ(std::string(Find(l, Action::SymbolsMode)->hotkey), std::string("L2+TRI"));
    CHECK_EQ(std::string(Find(l, Action::SpecialsMode)->hotkey), std::string("L3"));
    CHECK_EQ(std::string(Find(l, Action::Done)->hotkey), std::string("R2"));
    CHECK_EQ(std::string(KeyAt(LayoutFor(Family::Latin, Case::Upper, 0), 1, 0)->label), std::string("Q"));
    // Language switch, menu and motion control are gaps here.
    CHECK(!Enabled(Find(l, Action::Keyboard)));
    CHECK(!Enabled(Find(l, Action::Menu)));
    CHECK(!Enabled(Find(l, Action::Settings)));
}

void TextChecks() {
    std::printf("text\n");
    CHECK(Utf16("a\xC3\xA0\xE2\x82\xAC\xF0\x9F\x98\x80") == u"aà€\U0001F600");
    CHECK_EQ(Utf8(u"aà€\U0001F600"), std::string("a\xC3\xA0\xE2\x82\xAC\xF0\x9F\x98\x80"));
    CHECK(Utf16("\xC3") == u"�");  // truncated sequence

    Keyboard kb(u"My level", 20);
    CHECK(kb.text() == u"My level");
    CHECK_EQ(kb.cursor(), size_t(8));
    CHECK_EQ(Label(kb), std::string("q"));  // starts on q, as on the PS4
    CHECK(kb.Activate() == Outcome::None);
    CHECK(kb.text() == u"My levelq");
    kb.Backspace();
    CHECK(kb.text() == u"My level");
    kb.CursorHome();
    CHECK_EQ(kb.cursor(), size_t(0));
    kb.Delete();
    CHECK(kb.text() == u"y level");
    kb.CursorEnd();
    kb.CursorLeft();
    kb.CursorLeft();
    kb.Space();
    CHECK(kb.text() == u"y lev el");
    CHECK_EQ(kb.cursor(), size_t(6));
    kb.CursorRight();
    CHECK_EQ(kb.cursor(), size_t(7));
    const uint64_t edits = kb.editCount();
    kb.CursorRight();
    kb.CursorRight();  // already at the end: no change
    CHECK_EQ(kb.editCount(), edits + 1);

    // The limit, in UTF-16 units.
    Keyboard lim(u"abc", 4);
    CHECK(lim.Type(u"d"));
    CHECK(lim.text() == u"abcd");
    CHECK(!lim.Type(u"e"));
    CHECK(lim.text() == u"abcd");
    CHECK(lim.Activate() == Outcome::None);  // q doesn't fit either
    CHECK(lim.text() == u"abcd");
    Keyboard clamp(u"abcdef", 3);
    CHECK(clamp.text() == u"abc");
    Keyboard pairCut(u"ab\U0001F600", 3);  // keeping 3 units would split the pair
    CHECK(pairCut.text() == u"ab");
    Keyboard emoji(u"a\U0001F600", 10);
    emoji.Backspace();
    CHECK(emoji.text() == u"a");
    Keyboard emoji2(u"a\U0001F600b", 10);
    emoji2.CursorHome();
    emoji2.CursorRight();
    emoji2.CursorRight();  // over the whole pair
    CHECK_EQ(emoji2.cursor(), size_t(3));
    emoji2.CursorLeft();
    CHECK_EQ(emoji2.cursor(), size_t(1));
    emoji2.Delete();
    CHECK(emoji2.text() == u"ab");
    Keyboard full(u"abc", 4);
    CHECK(!full.Type(u"\U0001F600"));  // 2 units, 1 left
    Keyboard zero(u"", 0);
    CHECK_EQ(zero.maxLength(), 1u);

    // Typing filters control characters; line breaks only when multiline.
    Keyboard one(u"", 10);
    CHECK(one.Type(u"a\r\nb\tc"));
    CHECK(one.text() == u"abc");
    one.NewLine();
    CHECK(one.text() == u"abc");
    Keyboard multi(u"", 30, true);
    CHECK(multi.Type(u"ab\r\ncdef"));
    CHECK(multi.text() == u"ab\ncdef");
    multi.CursorUp();  // from the end of "cdef" (column 4) to the end of "ab"
    CHECK_EQ(multi.cursor(), size_t(2));
    multi.CursorDown();
    CHECK_EQ(multi.cursor(), size_t(5));
    // Up and down never stop between the halves of a surrogate pair.
    Keyboard pair(u"\U0001F600\nx", 30, true);  // an emoji (2 units) on line 1, the cursor after "x"
    pair.CursorUp();                              // column 1 would be inside the pair
    CHECK_EQ(pair.cursor(), size_t(0));
    Keyboard pairDown(u"x\n\U0001F600", 30, true);
    pairDown.CursorUp();                          // from the end of line 2 to after "x" (column 1)
    CHECK_EQ(pairDown.cursor(), size_t(1));
    pairDown.CursorDown();
    CHECK_EQ(pairDown.cursor(), size_t(2));       // the start of line 2, not inside the pair
    multi.CursorHome();
    CHECK_EQ(multi.cursor(), size_t(3));
    multi.NewLine();
    CHECK(multi.text() == u"ab\n\ncdef");

    // ArrowUp/ArrowDown keys: start/end on a single line.
    Keyboard arrows(u"hello", 10);
    const Layout& l = arrows.layout();
    CHECK(arrows.Select(Find(l, Action::ArrowUp)->row, Find(l, Action::ArrowUp)->col));
    arrows.Activate();
    CHECK_EQ(arrows.cursor(), size_t(0));
    CHECK(arrows.Select(Find(l, Action::ArrowDown)->row, Find(l, Action::ArrowDown)->col));
    arrows.Activate();
    CHECK_EQ(arrows.cursor(), size_t(5));
    CHECK(arrows.Select(Find(l, Action::ArrowLeft)->row, Find(l, Action::ArrowLeft)->col));
    arrows.Activate();
    CHECK_EQ(arrows.cursor(), size_t(4));
    CHECK(arrows.Select(Find(l, Action::Done)->row, Find(l, Action::Done)->col));
    CHECK(arrows.Activate() == Outcome::Done);
    CHECK(!arrows.Select(Find(l, Action::Menu)->row, Find(l, Action::Menu)->col));  // a gap
}

void NavigationChecks() {
    std::printf("navigation\n");
    Keyboard kb(u"", 50);
    // Wrap on a fresh press, stop on a repeat.
    kb.Move(0, -1, false);
    CHECK_EQ(Label(kb), std::string("p"));
    kb.Move(0, 1, true);
    CHECK_EQ(Label(kb), std::string("p"));  // held: stays at the edge
    kb.Move(0, 1, false);
    CHECK_EQ(Label(kb), std::string("q"));
    kb.Move(-1, 0, false);
    CHECK_EQ(Label(kb), std::string("1"));
    kb.Move(-1, 0, true);
    CHECK_EQ(Label(kb), std::string("1"));
    kb.Move(-1, 0, false);  // wraps to the bottom row
    CHECK(SelAction(kb) == Action::ArrowDown);
    // Across the wide Space key and the gap after it.
    const Layout& l = kb.layout();
    const KeySpec* space = Find(l, Action::Space);
    CHECK(kb.Select(space->row, space->col));
    kb.Move(0, 1, false);
    CHECK(SelAction(kb) == Action::Backspace);
    kb.Move(0, -1, false);
    CHECK(SelAction(kb) == Action::Space);
    kb.Move(0, -1, false);
    CHECK(SelAction(kb) == Action::SpecialsMode);
    // Down from n lands on Space, then on the nearest key below (the gaps under Space are skipped).
    CHECK(kb.Select(3, 5));
    CHECK_EQ(Label(kb), std::string("n"));
    kb.Move(1, 0, false);
    CHECK(SelAction(kb) == Action::Space);
    kb.Move(1, 0, false);
    CHECK(SelAction(kb) == Action::ArrowRight);
    kb.Move(-1, 0, false);
    CHECK(SelAction(kb) == Action::Space);
    // The column survives through a wide key when the next row has a key there.
    CHECK(kb.Select(3, 4));  // b
    kb.Move(1, 0, false);
    kb.Move(-1, 0, false);
    CHECK_EQ(Label(kb), std::string("b"));
    // Mouse select of a gap is refused.
    CHECK(!kb.Select(5, 5));
    CHECK(!kb.Select(9, 9));
}

void ModeChecks() {
    std::printf("modes\n");
    // Shift: one letter, then caps lock, then off.
    Keyboard kb(u"", 50);
    kb.Shift();
    CHECK(kb.caseState() == Case::Upper);
    kb.Activate();
    CHECK(kb.text() == u"Q");
    CHECK(kb.caseState() == Case::Lower);
    kb.Shift();
    kb.Shift();
    CHECK(kb.caseState() == Case::CapsLock);
    kb.Activate();
    kb.Move(0, 1);
    kb.Activate();
    CHECK(kb.text() == u"QQW");
    CHECK(kb.caseState() == Case::CapsLock);
    kb.Shift();
    CHECK(kb.caseState() == Case::Lower);
    kb.Shift();
    CHECK(kb.Type(u"x"));  // typing on the physical keyboard ends the one-letter shift too
    CHECK(kb.caseState() == Case::Lower);
    // The shift key itself cycles.
    const Layout& l = kb.layout();
    CHECK(kb.Select(Find(l, Action::Shift)->row, Find(l, Action::Shift)->col));
    kb.Activate();
    CHECK(kb.caseState() == Case::Upper);
    CHECK(SelAction(kb) == Action::Shift);

    // Symbols: the @#: key keeps the cursor on itself (now ABC); pairs put the cursor inside.
    Keyboard sym(u"", 50);
    const KeySpec* toSym = Find(sym.layout(), Action::SymbolsMode);
    CHECK(sym.Select(toSym->row, toSym->col));
    sym.Activate();
    CHECK(sym.family() == Family::Symbols);
    CHECK(SelAction(sym) == Action::SymbolsMode);
    CHECK_EQ(Label(sym), std::string("ABC"));
    sym.Shift();  // no case on the symbol pages
    CHECK(sym.caseState() == Case::Lower);
    CHECK(sym.Select(0, 8));
    CHECK_EQ(Label(sym), std::string("()"));
    sym.Activate();
    CHECK(sym.text() == u"()");
    CHECK_EQ(sym.cursor(), size_t(1));
    Keyboard tight(u"abc", 4);
    tight.ToggleSymbols();
    CHECK(tight.Select(0, 8));
    tight.Activate();
    CHECK(tight.text() == u"abc");  // both halves or nothing
    // Next page and back: the cursor stays on the page key.
    const KeySpec* next = Find(sym.layout(), Action::PageNext);
    CHECK(sym.Select(next->row, next->col));
    sym.Activate();
    CHECK_EQ(sym.page(), 1);
    CHECK(SelAction(sym) == Action::PagePrev);
    sym.Activate();
    CHECK_EQ(sym.page(), 0);
    CHECK(SelAction(sym) == Action::PageNext);
    // Up and down leave the two-row page key past both of its rows.
    {
        Keyboard pk(u"", 50);
        pk.ToggleSymbols();
        const KeySpec* pn = Find(pk.layout(), Action::PageNext);  // ">" over rows 2 and 3, column 9
        CHECK(pk.Select(pn->row + 1, pn->col));                    // entered from its second row
        pk.Move(-1, 0, false);
        CHECK_EQ(pk.row(), pn->row - 1);                           // the row above it ("&")
        CHECK(pk.Select(pn->row, pn->col));
        pk.Move(1, 0, false);
        CHECK_EQ(pk.row(), pn->row + 2);                           // the row below it
        CHECK(SelAction(pk) == Action::Backspace);
    }
    sym.ToggleSymbols();  // L2+Triangle back to letters, from a typing key
    CHECK(sym.family() == Family::Latin);
    CHECK(Enabled(sym.selected()));

    // Accents: the à key leads to the 7-row page and back, the cursor on the mode key.
    Keyboard acc(u"", 50);
    const KeySpec* toAcc = Find(acc.layout(), Action::SpecialsMode);
    CHECK(acc.Select(toAcc->row, toAcc->col));
    acc.Activate();
    CHECK(acc.family() == Family::Specials);
    CHECK_EQ(int(acc.layout().rows), 7);
    CHECK(SelAction(acc) == Action::SpecialsMode);
    CHECK_EQ(acc.row(), 5);
    acc.Move(-1, 0);
    acc.Activate();
    CHECK(!acc.text().empty());  // an accented letter
    acc.CursorHome();
    acc.Shift();
    acc.Activate();
    CHECK(acc.text().size() == 2 && acc.text()[0] != acc.text()[1]);  // the upper-case one first
    CHECK(acc.Select(5, 2));
    acc.Activate();
    CHECK(acc.family() == Family::Latin);
    CHECK(SelAction(acc) == Action::SpecialsMode);
    CHECK_EQ(acc.row(), 4);
    // From Specials, @#: goes to the symbols.
    acc.ToggleSpecials();
    acc.ToggleSymbols();
    CHECK(acc.family() == Family::Symbols);
    // A shortcut keeps the cursor where it is; the function rows stay the function rows.
    Keyboard sc(u"", 50);
    const KeySpec* bs = Find(sc.layout(), Action::Backspace);
    CHECK(sc.Select(bs->row, bs->col));
    sc.ToggleSpecials();
    CHECK(SelAction(sc) == Action::Backspace);
    sc.ToggleSpecials();
    CHECK(SelAction(sc) == Action::Backspace);
    Keyboard sc2(u"", 50);  // on q
    sc2.ToggleSpecials();
    CHECK_EQ(sc2.row(), 1);
    CHECK_EQ(sc2.col(), 0);
    CHECK(sc2.selected()->action == Action::Character);
}

void RepeaterChecks() {
    std::printf("repeater\n");
    Repeater r(400, 70);
    CHECK_EQ(r.Update(true, 1000), 1);
    CHECK_EQ(r.Update(true, 1200), 0);
    CHECK_EQ(r.Update(true, 1400), 2);
    CHECK_EQ(r.Update(true, 1450), 0);
    CHECK_EQ(r.Update(true, 1470), 2);
    CHECK_EQ(r.Update(false, 1480), 0);
    CHECK_EQ(r.Update(true, 1490), 1);
}

// ---- Suggestions (ui/osk_words) ----

const char* kTestList =
    "# a small list for the checks\n"
    "the\t1000\nthat\t900\nthey\t800\nthere\t700\nthis\t600\nthen\t500\nthem\t400\n"
    "I\t5000\nI'm\t300\ndon't\t250\nSackboy\t50\nMonday\t40\nsack\t30\nsackcloth\t1\n"
    "caf\xC3\xA9\t20\nco-op\t15\nsomething\t100\nso\t2000\nhello\t90\n";

using Strings = std::vector<std::u16string>;

void WordsChecks() {
    std::printf("words\n");
    // What a word is: letters (accents too), ' or - between letters, two letters or more;
    // numbers, name tags and addresses aren't words.
    CHECK(WordsIn(u"Hello, don't co-op lvl2 @sack www.example.com a I x2 café-au-lait 'quoted' end.") ==
          (Strings{u"Hello", u"don't", u"co-op", u"café-au-lait", u"quoted", u"end"}));
    CHECK(WordsIn(u"my_level 2nd -dash- re-") == (Strings{u"dash", u"re"}));
    // The part a suggestion completes: up to the cursor, which is at the end of a word.
    size_t at = 99;
    CHECK(TypedPart(u"hel", 3) == u"hel");
    CHECK(TypedPart(u"Hi there", 8, &at) == u"there");
    CHECK_EQ(int(at), 3);
    CHECK(TypedPart(u"sackboy", 4).empty());  // the middle of a word
    CHECK(TypedPart(u"sack boy", 4) == u"sack");
    CHECK(TypedPart(u"hello ", 6).empty());   // after a space
    CHECK(TypedPart(u"don'", 4) == u"don'");
    CHECK(TypedPart(u"'hel", 4) == u"hel");   // an opening quote isn't part of it
    CHECK(TypedPart(u"@sa", 3).empty());
    CHECK(TypedPart(u"lvl2ab", 6).empty());
    CHECK(TypedPart(u"www.goo", 7).empty());
    CHECK(TypedPart(u"", 0).empty());
    // Capitals follow what's typed; a word written with a capital keeps it.
    CHECK(FollowCase(u"sackboy", u"sa") == u"sackboy");
    CHECK(FollowCase(u"sackboy", u"Sa") == u"Sackboy");
    CHECK(FollowCase(u"sackboy", u"SAC") == u"SACKBOY");
    CHECK(FollowCase(u"Sackboy", u"sa") == u"Sackboy");
    CHECK(FollowCase(u"I'm", u"i") == u"I'm");
    CHECK(FollowCase(u"café", u"CA") == u"CAFÉ");
    CHECK(Lower(u"ÉİŸАΑ") == u"éiÿаα");

    Words w;
    w.LoadList(kTestList);
    CHECK_EQ(int(w.listSize()), 19);
    // The list alone: most common first, at most six, none longer than the room left, never
    // what's already typed.
    CHECK(w.Suggest(u"th", 100) == (Strings{u"the", u"that", u"they", u"there", u"this", u"then"}));
    CHECK(w.Suggest(u"th", 4) == (Strings{u"the", u"that", u"they", u"this", u"then", u"them"}));
    CHECK(w.Suggest(u"the", 100) == (Strings{u"they", u"there", u"then", u"them"}));
    CHECK(w.Suggest(u"i", 100) == (Strings{u"I", u"I'm"}));
    CHECK(w.Suggest(u"Th", 100)[0] == u"The");
    CHECK(w.Suggest(u"CAF", 100) == (Strings{u"CAFÉ"}));
    CHECK(w.Suggest(u"don’", 100) == (Strings{u"don't"}));
    CHECK(w.Suggest(u"sa", 100) == (Strings{u"Sackboy", u"sack", u"sackcloth"}));
    CHECK(w.Suggest(u"zz", 100).empty());
    CHECK(w.Suggest(u"", 100).empty());
    // Learned words first, most used first.
    CHECK(w.Learn(u"", u"them them them they"));
    CHECK(w.Suggest(u"th", 100) == (Strings{u"them", u"they", u"the", u"that", u"there", u"this"}));
    // A word that isn't in the list: from its second use, as last typed.
    CHECK(w.Learn(u"", u"thrumble"));
    CHECK(w.Suggest(u"thr", 100).empty());
    CHECK(w.Learn(u"", u"Thrumble"));
    CHECK(w.Suggest(u"thr", 100) == (Strings{u"Thrumble"}));
    // A list word is kept as the list spells it, whatever the capitals typed.
    CHECK(w.Learn(u"", u"The"));
    CHECK_EQ(int(w.LearnedCount(u"the")), 1);
    CHECK(w.SaveLearned().find("\tthe\n") != std::string::npos);
    CHECK(w.SaveLearned().find("\tThe\n") == std::string::npos);
    // Only the words added to the text the keyboard opened with count.
    CHECK(!w.Learn(u"My level about Sackboy", u"My level about Sackboy"));
    CHECK(w.Learn(u"My level about Sackboy", u"My level about Sackboy and Sackboy"));
    CHECK_EQ(int(w.LearnedCount(u"sackboy")), 1);
    CHECK_EQ(int(w.LearnedCount(u"level")), 0);
    CHECK_EQ(int(w.LearnedCount(u"and")), 1);  // not in the list: one use, counted but not suggested
    CHECK(w.Suggest(u"an", 100).empty());
    // The curly apostrophe is the straight one.
    CHECK(w.Learn(u"", u"don’t"));
    CHECK_EQ(int(w.LearnedCount(u"don't")), 1);

    // The file: written most used first, read back the same.
    const std::string saved = w.SaveLearned();
    CHECK(saved.find("3\tthem\n") != std::string::npos);
    CHECK(saved.find("2\tThrumble\n") != std::string::npos);
    Words w2;
    w2.LoadList(kTestList);
    w2.LoadLearned(saved);
    CHECK(w2.SaveLearned() == saved);
    CHECK(w2.Suggest(u"th", 100) == w.Suggest(u"th", 100));
    // A hand-edited file: a BOM, CRLF, comments, junk, a word listed twice, non-words.
    Words w3;
    w3.LoadList(kTestList);
    w3.LoadLearned("\xEF\xBB\xBF# mine\r\n3\tthem\r\nbad line\r\n-1\tfoo\r\n2 Bunkum\r\n1\tx\r\n4\tthem\r\n\r\n5\tlvl2\r\n0\tzero\r\n");
    CHECK_EQ(int(w3.learnedSize()), 2);
    CHECK_EQ(int(w3.LearnedCount(u"them")), 7);
    CHECK_EQ(int(w3.LearnedCount(u"bunkum")), 2);
    CHECK(w3.Suggest(u"bu", 100) == (Strings{u"Bunkum"}));
    // At most 10,000 learned words: the least used go first.
    Words w4;
    std::u16string many = u"favourite favourite ";
    for (int i = 0; i < 10001; ++i) {
        std::u16string word;
        for (int n = i, k = 0; k < 4; ++k, n /= 26) word.push_back(char16_t(u'a' + n % 26));
        many += word + u' ';
    }
    CHECK(w4.Learn(u"", many));
    CHECK_EQ(int(w4.learnedSize()), int(Words::kMaxLearned));
    CHECK_EQ(int(w4.LearnedCount(u"favourite")), 2);
}

// The keyboard model with the test list's suggestions.
Keyboard Suggesting(const Words& w, std::u16string text, uint32_t maxLength) {
    Keyboard kb(std::move(text), maxLength);
    kb.SetSuggester([&w](std::u16string_view typed, size_t room) { return w.Suggest(typed, room); });
    return kb;
}

void BarChecks() {
    std::printf("prediction bar\n");
    Words w;
    w.LoadList(kTestList);
    Keyboard kb = Suggesting(w, u"", 40);
    CHECK(kb.suggestions().empty());
    CHECK(kb.Type(u"th"));
    CHECK(kb.suggestions() == (Strings{u"the", u"that", u"they", u"there", u"this", u"then"}));
    // Up from the top row goes into the bar, onto the first suggestion.
    CHECK(kb.Select(0, 3));
    kb.Move(-1, 0);
    CHECK(kb.inBar());
    CHECK_EQ(kb.barIndex(), 0);
    kb.Move(0, 1);
    CHECK_EQ(kb.barIndex(), 1);
    kb.Move(0, -1);
    kb.Move(0, -1);  // a fresh press wraps
    CHECK_EQ(kb.barIndex(), 5);
    kb.Move(0, 1, true);  // a held one stops at the end
    CHECK_EQ(kb.barIndex(), 5);
    kb.Move(-1, 0);  // up does nothing
    CHECK_EQ(kb.barIndex(), 5);
    kb.Move(1, 0);  // down: back to the key it came from
    CHECK(!kb.inBar());
    CHECK_EQ(kb.row(), 0);
    CHECK_EQ(kb.col(), 3);
    // Cross picks: the word replaces what's typed, a space follows, the selection goes back.
    kb.Move(-1, 0);
    kb.Move(0, 1);
    CHECK(kb.Activate() == Outcome::None);
    CHECK(kb.text() == u"that ");
    CHECK_EQ(int(kb.cursor()), 5);
    CHECK(!kb.inBar());
    CHECK_EQ(kb.row(), 0);
    CHECK_EQ(kb.col(), 3);
    CHECK(kb.suggestions().empty());
    // With an empty bar, up from the top row wraps to the bottom as before.
    kb.Move(-1, 0);
    CHECK(!kb.inBar());
    CHECK_EQ(kb.row(), 5);

    // Punctuation straight after a pick goes before its space; a space out of habit is
    // swallowed, once; anything else ends that.
    Keyboard p = Suggesting(w, u"", 40);
    p.Type(u"hel");
    CHECK(p.Pick(0));
    CHECK(p.text() == u"hello ");
    CHECK(p.Type(u"."));
    CHECK(p.text() == u"hello. ");
    CHECK_EQ(int(p.cursor()), 7);
    p.Space();
    CHECK(p.text() == u"hello. ");
    p.Space();
    CHECK(p.text() == u"hello.  ");
    Keyboard q = Suggesting(w, u"", 40);
    q.Type(u"hel");
    q.Pick(0);
    q.Type(u" ");  // the PC keyboard's space bar
    CHECK(q.text() == u"hello ");
    q.Type(u"x");
    q.Type(u".");
    CHECK(q.text() == u"hello x.");
    Keyboard keyPunct = Suggesting(w, u"", 40);
    keyPunct.Type(u"hel");
    keyPunct.Pick(0);
    keyPunct.Backspace();  // deletes the space only
    CHECK(keyPunct.text() == u"hello");
    keyPunct.Type(u".");
    CHECK(keyPunct.text() == u"hello.");
    // A space already after the word: no second one, the cursor steps over it.
    Keyboard mid = Suggesting(w, u"hel world", 40);
    mid.CursorHome();
    for (int i = 0; i < 3; ++i) mid.CursorRight();
    CHECK(mid.suggestions() == (Strings{u"hello"}));
    CHECK(mid.Pick(0));
    CHECK(mid.text() == u"hello world");
    CHECK_EQ(int(mid.cursor()), 6);
    // No room for the space: the word alone. No room for the word: not offered.
    Keyboard full = Suggesting(w, u"", 5);
    full.Type(u"hel");
    CHECK(full.Pick(0));
    CHECK(full.text() == u"hello");
    full.Type(u".");
    CHECK(full.text() == u"hello");
    Keyboard tight = Suggesting(w, u"", 4);
    tight.Type(u"hel");
    CHECK(tight.suggestions().empty());
    // The cursor inside a word: nothing to suggest.
    Keyboard inside = Suggesting(w, u"", 40);
    inside.Type(u"sackboy");
    for (int i = 0; i < 3; ++i) inside.CursorLeft();
    CHECK(inside.suggestions().empty());
    // Capitals follow what's typed; a pick ends a one-letter shift.
    Keyboard caps = Suggesting(w, u"", 40);
    caps.Type(u"Hel");
    caps.Shift();
    CHECK(caps.caseState() == Case::Upper);
    caps.Pick(0);
    CHECK(caps.text() == u"Hello ");
    CHECK(caps.caseState() == Case::Lower);
    // The bar changing under the selection: it stays in the bar on the first suggestion, and
    // goes back to the key when the bar empties.
    Keyboard ch = Suggesting(w, u"", 40);
    ch.Type(u"th");
    ch.Select(0, 4);
    ch.Move(-1, 0);
    ch.Move(0, 1);
    ch.Move(0, 1);
    CHECK_EQ(ch.barIndex(), 2);
    ch.Type(u"e");
    CHECK(ch.suggestions() == (Strings{u"they", u"there", u"then", u"them"}));
    CHECK_EQ(ch.barIndex(), 0);
    ch.Backspace();
    ch.Backspace();
    ch.Backspace();
    CHECK(ch.suggestions().empty());
    CHECK(!ch.inBar());
    CHECK_EQ(ch.row(), 0);
    CHECK_EQ(ch.col(), 4);
    // The mouse: hovering a suggestion selects it, a key takes the selection back.
    ch.Type(u"th");
    CHECK(ch.SelectSuggestion(1));
    CHECK_EQ(ch.barIndex(), 1);
    CHECK(!ch.SelectSuggestion(9));
    CHECK(ch.Select(1, 0));
    CHECK(!ch.inBar());
    // No suggester: never any.
    Keyboard none(u"", 40);
    none.Type(u"th");
    CHECK(none.suggestions().empty());
    CHECK(!none.Pick(0));
}

// ---- Overlay ----

uint64_t g_now = 1000;
uint64_t Clock() { return g_now; }

struct Answers {
    int count = 0;
    bool confirmed = false;
    std::u16string text;
};

AnswerFn Recorder(Answers& a) {
    return [&a](const Answer& ans) {
        ++a.count;
        a.confirmed = ans.confirmed;
        a.text = ans.text;
    };
}

ps4::PadData g_pad{};

void Pad(uint32_t buttons, int sx = 0, int sy = 0) {
    g_pad.buttons = buttons;
    g_pad.leftStick = {uint8_t(128 + sx), uint8_t(128 + sy)};
    g_pad.rightStick = {128, 128};
    g_now += 16;
    Draw();
}

std::u16string Text() {
    std::u16string t;
    testing::WithKeyboard([&](Keyboard& kb) { t = kb.text(); });
    return t;
}

SDL_Event KeyEvent(SDL_EventType type, SDL_Keycode key, bool repeat = false) {
    SDL_Event e{};
    e.type = type;
    e.key.key = key;
    e.key.repeat = repeat;
    return e;
}

// The right stick (moves the panel), the left one centred.
void PadR(uint32_t buttons, int rx, int ry) {
    g_pad.buttons = buttons;
    g_pad.leftStick = {128, 128};
    g_pad.rightStick = {uint8_t(128 + rx), uint8_t(128 + ry)};
    g_now += 16;
    Draw();
}

// The mouse at (x, y) in the window; button: 0 = no change, 1 = left down, 2 = left up. One
// frame after each event, so ImGui sees the move and the click separately.
void Mouse(SDL_Window* w, float x, float y, int button = 0) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(w);
    e.motion.x = x;
    e.motion.y = y;
    HandleEvent(e);
    Pad(0);
    if (!button) return;
    SDL_Event b{};
    b.type = button == 1 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    b.button.windowID = SDL_GetWindowID(w);
    b.button.button = SDL_BUTTON_LEFT;
    b.button.down = button == 1;
    b.button.clicks = 1;
    b.button.x = x;
    b.button.y = y;
    HandleEvent(b);
    Pad(0);
}

void TypeText(const char* utf8) {
    SDL_Event e{};
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = utf8;
    HandleEvent(e);
}

Strings Bar() {
    Strings s;
    testing::WithKeyboard([&](Keyboard& kb) { s = kb.suggestions(); });
    return s;
}

void SuggestionChecks(SDL_Window* window) {
    std::printf("suggestions\n");
    testing::SetWordList(kTestList);
    Request req;
    req.title = u"Enter Your Message";
    req.maxLength = 96;
    // R3 picks the first suggestion.
    Answers a1;
    Pad(0);
    Show(req, Recorder(a1));
    Pad(0);
    TypeText("th");
    CHECK(Bar() == (Strings{u"the", u"that", u"they", u"there", u"this", u"then"}));
    Pad(ps4::PAD_R3);
    CHECK(Text() == u"the ");
    Pad(0);
    // Up from the top row into the bar, right, Cross picks; the selection is back on the key.
    TypeText("th");
    testing::WithKeyboard([](Keyboard& kb) { kb.Select(0, 2); });
    Pad(ps4::PAD_UP);
    bool inBar = false;
    testing::WithKeyboard([&](Keyboard& kb) { inBar = kb.inBar(); });
    CHECK(inBar);
    Pad(0);
    Pad(ps4::PAD_LEFT);  // wraps to the last
    Pad(0);
    Pad(ps4::PAD_CROSS);
    CHECK(Text() == u"the then ");
    int row = -1;
    testing::WithKeyboard([&](Keyboard& kb) {
        inBar = kb.inBar();
        row = kb.row();
    });
    CHECK(!inBar);
    CHECK_EQ(row, 0);
    Pad(0);
    // Tab picks the first; a habit space after it is swallowed.
    TypeText("he");
    CHECK(HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_TAB)));
    TypeText(" ");
    CHECK(Text() == u"the then hello ");
    // A click on a suggestion picks it.
    TypeText("so");
    Pad(0);
    float x = 0.f, y = 0.f;
    CHECK(testing::SuggestionCentre(0, x, y));
    Mouse(window, x, y);
    Mouse(window, x, y, 1);
    Mouse(window, x, y, 2);
    CHECK(Text() == u"the then hello something ");
    // Confirmed: its words are learned.
    Pad(ps4::PAD_R2);
    CHECK(a1.confirmed);
    CHECK_EQ(int(testing::LearnedCount(u"then")), 1);
    CHECK_EQ(int(testing::LearnedCount(u"something")), 1);
    Pad(0);
    // Learned words come first.
    Answers a2;
    Show(req, Recorder(a2));
    Pad(0);
    TypeText("th");
    CHECK(Bar() == (Strings{u"the", u"then", u"that", u"they", u"there", u"this"}));
    // Cancelled: nothing learned.
    TypeText("em");
    Pad(ps4::PAD_CIRCLE);
    CHECK(!a2.confirmed);
    CHECK_EQ(int(testing::LearnedCount(u"them")), 0);
    Pad(0);
    // Words already in the text it opened with aren't counted again.
    Request edit = req;
    edit.text = u"hello there";
    Answers a3;
    Show(edit, Recorder(a3));
    Pad(0);
    TypeText(" again");
    Pad(ps4::PAD_R2);
    CHECK_EQ(int(testing::LearnedCount(u"hello")), 1);
    CHECK_EQ(int(testing::LearnedCount(u"there")), 0);
    CHECK_EQ(int(testing::LearnedCount(u"again")), 1);
    Pad(0);
    // A password field: no suggestions, nothing learned.
    Request pw = req;
    pw.password = true;
    Answers a4;
    Show(pw, Recorder(a4));
    Pad(0);
    TypeText("th");
    CHECK(Bar().empty());
    Pad(ps4::PAD_R3);  // nothing to pick
    CHECK(Text() == u"th");
    Pad(ps4::PAD_R2);
    CHECK_EQ(int(testing::LearnedCount(u"th")), 0);
    Pad(0);
    // Only as many as fit in the bar.
    testing::SetWordList(
        "internationalization\t10\ninternationalisation\t9\ninterconnectedness\t8\ninterdisciplinary\t7\n"
        "intercontinental\t6\ninterchangeability\t5\n");
    Answers a5;
    Show(req, Recorder(a5));
    Pad(0);
    TypeText("inter");
    const size_t shown = Bar().size();
    CHECK(shown >= 1 && shown < 6);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    testing::SetWordList(nullptr);  // off again (no files)
}

void PlaceChecks(SDL_Window* window, const Request& req) {
    std::printf("place\n");
    float px = 0.f, py = 0.f, qx = 0.f, qy = 0.f;
    // The right stick moves the panel, faster the further it's pushed, and stops at the edges.
    testing::SetPlace(0.5f, 0.5f);
    Answers a1;
    Pad(0);
    Show(req, Recorder(a1));
    Pad(0);
    for (int i = 0; i < 6; ++i) PadR(0, 127, 0);
    testing::GetPlace(px, py);
    CHECK(px > 0.55f && px < 0.62f);  // 1280x720: ~0.0128 of the room a 16 ms frame
    CHECK(py == 0.5f);
    PadR(0, 12, -12);  // inside the deadzone (0.13 of full tilt)
    testing::GetPlace(qx, qy);
    CHECK(qx == px && qy == py);
    for (int i = 0; i < 200; ++i) PadR(0, 127, 127);
    testing::GetPlace(px, py);
    CHECK(px == 1.f && py == 1.f);
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(a1.count, 1);
    // The next keyboard opens where the last was left; a stick held as it opens moves nothing
    // until it has been let go.
    Answers a2;
    PadR(0, -127, 0);
    Show(req, Recorder(a2));
    PadR(0, -127, 0);
    PadR(0, -127, 0);
    testing::GetPlace(px, py);
    CHECK(px == 1.f && py == 1.f);
    Pad(0);
    PadR(0, -127, 0);
    PadR(0, -127, 0);
    testing::GetPlace(px, py);
    CHECK(px < 1.f && py == 1.f);
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    // The mouse drags it by the title row. At 1280x720 the centred panel's corner is at
    // (375, 184) and its title row is 38 px high.
    testing::SetPlace(0.5f, 0.5f);
    Answers a3;
    Pad(0);
    Show(req, Recorder(a3));
    Pad(0);
    Mouse(window, 400.f, 200.f);
    Mouse(window, 400.f, 200.f, 1);
    Mouse(window, 100.f, 50.f);
    Mouse(window, 100.f, 50.f, 2);
    testing::GetPlace(px, py);
    CHECK(std::fabs(px - 75.f / 751.33f) < 0.01f);
    CHECK(std::fabs(py - 34.f / 368.f) < 0.01f);
    Mouse(window, 600.f, 600.f);  // released: it stays
    testing::GetPlace(qx, qy);
    CHECK(qx == px && qy == py);
    CHECK_EQ(a3.count, 0);  // dragging answers nothing
    Pad(ps4::PAD_CIRCLE);
    Pad(0);

    // The place lasts between runs: user\config\keyboard.ini, written when a keyboard closes,
    // read by the first keyboard of a run; a hand-edited file is read as far as it makes sense.
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path old = fs::current_path();
    const fs::path dir = fs::temp_directory_path() / ("osktest_files_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    fs::current_path(dir);
    testing::UseFiles(true);
    testing::SetWordList(kTestList);
    testing::ForgetFiles();
    testing::SetPlace(0.25f, 0.75f);
    Answers a4;
    Show(req, Recorder(a4));
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    std::ifstream ini("user\\config\\keyboard.ini");
    std::stringstream body;
    body << ini.rdbuf();
    ini.close();
    CHECK(body.str().find("x=0.2500\ny=0.7500\n") != std::string::npos);
    CHECK(!fs::exists("user\\config\\keyboard.ini.tmp"));
    testing::ForgetFiles();
    testing::GetPlace(px, py);
    CHECK(px == 0.5f && py == 0.5f);
    Answers a5;
    Show(req, Recorder(a5));
    Pad(0);
    testing::GetPlace(px, py);
    CHECK(px == 0.25f && py == 0.75f);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    {
        std::ofstream edit("user\\config\\keyboard.ini", std::ios::binary | std::ios::trunc);
        edit << "\xEF\xBB\xBFjunk\r\n x=0.1 \r\ny=abc\r\nz=0.9\r\n";
    }
    testing::ForgetFiles();
    Answers a6;
    Show(req, Recorder(a6));
    Pad(0);
    testing::GetPlace(px, py);
    CHECK(std::fabs(px - 0.1f) < 1e-6f && py == 0.5f);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    // The learned words too: user\config\learned_words.txt, written when text is confirmed.
    Answers a7;
    Show(req, Recorder(a7));
    Pad(0);
    TypeText(" hel");
    Pad(ps4::PAD_R3);
    Pad(ps4::PAD_R2);
    Pad(0);
    std::ifstream learned("user\\config\\learned_words.txt");
    std::stringstream words;
    words << learned.rdbuf();
    learned.close();
    CHECK(words.str().find("1\thello\n") != std::string::npos);
    testing::ForgetFiles();
    CHECK_EQ(int(testing::LearnedCount(u"hello")), 0);
    Answers a8;
    Show(req, Recorder(a8));
    Pad(0);
    CHECK_EQ(int(testing::LearnedCount(u"hello")), 1);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    testing::SetWordList(nullptr);
    testing::UseFiles(false);
    testing::ForgetFiles();
    fs::current_path(old);
    fs::remove_all(dir, ec);
}

void OverlayChecks() {
    std::printf("overlay\n");
    SDL_Window* window = SDL_CreateWindow("osktest", 1280, 720, SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        std::printf("  no renderer (%s): overlay checks skipped\n", SDL_GetError());
        CHECK(false);
        return;
    }
    CHECK(!Available());
    Attach(window, renderer);
    CHECK(Available());
    testing::SetClock(Clock);
    testing::SetPad(&g_pad);
    testing::UseFiles(false);  // nothing from or into this PC's user\config
    Draw();  // nothing up: no-op
    CHECK(!Visible());

    Request req;
    req.title = u"Level Name";
    req.text = u"My level";
    req.maxLength = 20;

    // Cross held from the game's menu when the keyboard opens types nothing.
    Answers a1;
    Pad(ps4::PAD_CROSS);
    const uint64_t id1 = Show(req, Recorder(a1));
    CHECK(id1 != 0);
    CHECK(Visible());
    Pad(ps4::PAD_CROSS);
    Pad(ps4::PAD_CROSS);
    CHECK(Text() == u"My level");
    Pad(0);
    Pad(ps4::PAD_CROSS);  // a fresh press types the selected key (q)
    CHECK(Text() == u"My levelq");
    Pad(0);
    Pad(ps4::PAD_SQUARE);  // backspace
    CHECK(Text() == u"My level");
    Pad(0);
    Pad(ps4::PAD_TRIANGLE);  // space
    CHECK(Text() == u"My level ");
    Pad(0);
    Pad(ps4::PAD_L1);  // cursor left
    int cursor = -1;
    testing::WithKeyboard([&](Keyboard& kb) { cursor = int(kb.cursor()); });
    CHECK_EQ(cursor, 8);
    Pad(0);
    Pad(ps4::PAD_R1);
    // L2 alone is shift (on release).
    Pad(0);
    Pad(ps4::PAD_L2);
    Case c = Case::Lower;
    testing::WithKeyboard([&](Keyboard& kb) { c = kb.caseState(); });
    CHECK(c == Case::Lower);
    Pad(0);
    testing::WithKeyboard([&](Keyboard& kb) { c = kb.caseState(); });
    CHECK(c == Case::Upper);
    // L2+Triangle is the symbols, not shift, and no space.
    Pad(ps4::PAD_L2);
    Pad(ps4::PAD_L2 | ps4::PAD_TRIANGLE);
    Pad(ps4::PAD_L2);
    Pad(0);
    Family f = Family::Latin;
    testing::WithKeyboard([&](Keyboard& kb) {
        f = kb.family();
        c = kb.caseState();
    });
    CHECK(f == Family::Symbols);
    CHECK(c == Case::Upper);
    CHECK(Text() == u"My level ");
    Pad(ps4::PAD_L3);  // accents
    Pad(0);
    testing::WithKeyboard([&](Keyboard& kb) { f = kb.family(); });
    CHECK(f == Family::Specials);
    Pad(ps4::PAD_L3);
    Pad(0);
    testing::WithKeyboard([&](Keyboard& kb) { f = kb.family(); });
    CHECK(f == Family::Latin);
    // D-pad: a press moves once; held, it repeats after the delay.
    testing::WithKeyboard([&](Keyboard& kb) { kb.Select(1, 0); });
    Pad(ps4::PAD_RIGHT);
    int col = -1;
    testing::WithKeyboard([&](Keyboard& kb) { col = kb.col(); });
    CHECK_EQ(col, 1);
    Pad(ps4::PAD_RIGHT);  // +16 ms: no repeat yet
    testing::WithKeyboard([&](Keyboard& kb) { col = kb.col(); });
    CHECK_EQ(col, 1);
    g_now += 400;
    Pad(ps4::PAD_RIGHT);
    testing::WithKeyboard([&](Keyboard& kb) { col = kb.col(); });
    CHECK_EQ(col, 2);
    Pad(0);
    // The stick moves too.
    Pad(0, 0, 100);
    int row = -1;
    testing::WithKeyboard([&](Keyboard& kb) { row = kb.row(); });
    CHECK_EQ(row, 2);
    Pad(0);
    // R2 confirms.
    Pad(ps4::PAD_R2);
    CHECK_EQ(a1.count, 1);
    CHECK(a1.confirmed);
    CHECK(a1.text == u"My level ");
    CHECK(!Visible());
    Close(id1);  // stale: ignored
    Pad(0);
    CHECK_EQ(a1.count, 1);

    // Circle cancels; a stick held at open is ignored until centred.
    Answers a2;
    Pad(0, 0, 120);
    Show(req, Recorder(a2));
    Pad(0, 0, 120);
    Pad(0, 0, 120);
    testing::WithKeyboard([&](Keyboard& kb) { row = kb.row(); });
    CHECK_EQ(row, 1);
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(a2.count, 1);
    CHECK(!a2.confirmed);
    CHECK(!Visible());

    // Close takes it down without an answer, before or after the presenter picked it up.
    Answers a3;
    Pad(0);
    const uint64_t id3 = Show(req, Recorder(a3));
    Close(id3);
    CHECK(!Visible());
    Pad(0);
    Answers a4;
    const uint64_t id4 = Show(req, Recorder(a4));
    Pad(0);
    Close(id4);
    Pad(ps4::PAD_R2);
    CHECK_EQ(a3.count, 0);
    CHECK_EQ(a4.count, 0);
    CHECK(!testing::WithKeyboard([](Keyboard&) {}));

    // A second Show replaces the first, which never answers.
    Answers a5, a6;
    Pad(0);
    Show(req, Recorder(a5));
    Pad(0);
    Show(req, Recorder(a6));
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(a5.count, 0);
    CHECK_EQ(a6.count, 1);

    // The done key with Cross answers too.
    Answers a7;
    Pad(0);
    Show(req, Recorder(a7));
    Pad(0);
    testing::WithKeyboard([&](Keyboard& kb) {
        const KeySpec* d = Find(kb.layout(), Action::Done);
        kb.Select(d->row, d->col);
    });
    Pad(ps4::PAD_CROSS);
    CHECK_EQ(a7.count, 1);
    CHECK(a7.confirmed);

    // Physical keys: text, Backspace, a fresh Enter confirms; a repeat doesn't; Esc cancels.
    Answers a8;
    Pad(0);
    Show(req, Recorder(a8));
    Pad(0);
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "Ab";
    CHECK(HandleEvent(text));
    CHECK(Text() == u"My levelAb");
    CHECK(HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_BACKSPACE)));
    CHECK(Text() == u"My levelA");
    CHECK(!HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_F9)));  // a debug hotkey stays one
    HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_RETURN, true));
    CHECK_EQ(a8.count, 0);
    HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_RETURN));
    CHECK_EQ(a8.count, 1);
    CHECK(a8.text == u"My levelA");
    CHECK(!HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_A)));  // nothing up: not ours
    Answers a9;
    Show(req, Recorder(a9));
    Pad(0);
    HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE));
    CHECK_EQ(a9.count, 1);
    CHECK(!a9.confirmed);

    // The limit holds for the pad too.
    Answers a10;
    Request small = req;
    small.text = u"abc";
    small.maxLength = 3;
    Show(small, Recorder(a10));
    Pad(0);
    Pad(ps4::PAD_CROSS);
    Pad(0);
    Pad(ps4::PAD_TRIANGLE);
    Pad(0);
    CHECK(Text() == u"abc");
    Pad(ps4::PAD_CIRCLE);

    PlaceChecks(window, req);
    SuggestionChecks(window);

    testing::SetPad(nullptr);
    testing::SetClock(nullptr);
    Attach(nullptr, nullptr);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

// ---- Screenshots ----

void FakeGame(SDL_Renderer* r, int w, int h) {
    // Sky, hills and a floor, roughly a level's colours, so the dimming and panel show up.
    for (int y = 0; y < h; y += 4) {
        const float t = float(y) / h;
        SDL_SetRenderDrawColor(r, Uint8(110 + 80 * t), Uint8(170 + 40 * t), Uint8(225 - 30 * t), 255);
        SDL_FRect band{0.f, float(y), float(w), 4.f};
        SDL_RenderFillRect(r, &band);
    }
    SDL_SetRenderDrawColor(r, 92, 140, 70, 255);
    for (int i = 0; i < 6; ++i) {
        SDL_FRect hill{w * (i / 6.f) - w * 0.05f, h * (0.55f + 0.04f * (i % 2)), w * 0.28f, h * 0.5f};
        SDL_RenderFillRect(r, &hill);
    }
    SDL_SetRenderDrawColor(r, 150, 105, 60, 255);
    SDL_FRect floor{0.f, h * 0.78f, float(w), h * 0.22f};
    SDL_RenderFillRect(r, &floor);
    SDL_SetRenderDrawColor(r, 235, 200, 140, 255);
    SDL_FRect sack{w * 0.2f, h * 0.62f, w * 0.05f, h * 0.16f};
    SDL_RenderFillRect(r, &sack);
}

int Shots(const char* dir) {
    struct State {
        const char* name;
        void (*setup)(Keyboard&);
        const char16_t* text;
        float placeX = 0.5f, placeY = 0.5f;  // where the panel sits
    };
    const State states[] = {
        {"lower", [](Keyboard&) {}, u""},
        {"upper", [](Keyboard& kb) { kb.Shift(); }, u""},
        {"caps", [](Keyboard& kb) { kb.Shift(); kb.Shift(); }, u""},
        {"symbols1", [](Keyboard& kb) { kb.ToggleSymbols(); kb.Select(2, 2); }, u"Level 1"},
        {"symbols2", [](Keyboard& kb) {
             kb.ToggleSymbols();
             kb.Press(KeySpec{0, 0, 1, 1, nullptr, nullptr, Action::PageNext});
             kb.Select(1, 3);
         }, u"Level 1"},
        {"specials1", [](Keyboard& kb) { kb.ToggleSpecials(); kb.Select(1, 1); }, u"Caf"},
        {"specials2", [](Keyboard& kb) {
             kb.ToggleSpecials();
             kb.Press(KeySpec{0, 0, 1, 1, nullptr, nullptr, Action::PageNext});
             kb.Select(0, 2);
         }, u"Caf"},
        {"text", [](Keyboard& kb) {
             for (int i = 0; i < 6; ++i) kb.CursorLeft();
             kb.Select(2, 4);
         }, u"My first sackboy level"},
        {"long", [](Keyboard& kb) {
             const KeySpec* d = KeyAt(kb.layout(), 5, 8);
             if (d) kb.Select(d->row, d->col);
         }, u"An extremely long level name that does not fit in the text field at all, so it scrolls"},
        // Word suggestions (the test list): the bar, the selection in it, and the panel moved.
        {"suggest", [](Keyboard& kb) { kb.Select(2, 4); }, u"Hello th"},
        {"suggestbar", [](Keyboard& kb) {
             kb.Select(0, 3);
             kb.Move(-1, 0);
             kb.Move(0, 1);
         }, u"Hello th"},
        {"corner", [](Keyboard& kb) { kb.Select(2, 4); }, u"I'm at the s", 0.f, 1.f},
    };
    const int sizes[][2] = {{1280, 720}, {1920, 1080}};
    int written = 0;
    for (const auto& sz : sizes) {
        SDL_Window* window = SDL_CreateWindow("osktest shot", sz[0], sz[1], SDL_WINDOW_HIDDEN);
        SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
        if (!renderer) {
            std::printf("no renderer: %s\n", SDL_GetError());
            return 1;
        }
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pw, ph);
        Attach(window, renderer);
        static ps4::PadData idle{};
        idle.leftStick = idle.rightStick = {128, 128};
        testing::SetPad(&idle);
        testing::SetClock(Clock);
        testing::UseFiles(false);
        testing::SetWordList(kTestList);
        for (const State& st : states) {
            testing::SetPlace(st.placeX, st.placeY);
            Request req;
            req.title = u"Level Name";
            req.text = st.text;
            req.maxLength = std::string(st.name) == "long" ? 128 : 64;
            if (std::string(st.name) == "lower") req.placeholder = u"Enter a name for your level";
            const uint64_t id = Show(req, nullptr);
            Draw();  // the presenter picks it up
            testing::WithKeyboard([&](Keyboard& kb) { st.setup(kb); });
            g_now += 16;
            SDL_SetRenderTarget(renderer, target);
            FakeGame(renderer, pw, ph);
            Draw();
            if (std::getenv("OSKTEST_DEBUG")) {
                const ImDrawData* dd = ImGui::GetDrawData();
                std::printf("  draw data: valid %d, %d lists, %d vertices, display %.0fx%.0f, scale %.2f\n",
                            dd ? dd->Valid : -1, dd ? dd->CmdLists.Size : -1, dd ? dd->TotalVtxCount : -1,
                            dd ? dd->DisplaySize.x : 0.f, dd ? dd->DisplaySize.y : 0.f, dd ? dd->FramebufferScale.x : 0.f);
            }
            SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
            SDL_SetRenderTarget(renderer, nullptr);
            char path[512];
            std::snprintf(path, sizeof(path), "%s\\osk_%dx%d_%s.png", dir, pw, ph, st.name);
            if (shot && SDL_SavePNG(shot, path)) {
                std::printf("wrote %s\n", path);
                ++written;
            } else {
                std::printf("couldn't write %s: %s\n", path, SDL_GetError());
            }
            if (shot) SDL_DestroySurface(shot);
            Close(id);
            Draw();
        }
        testing::SetPad(nullptr);
        Attach(nullptr, nullptr);
        SDL_DestroyTexture(target);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
    return written == int(std::size(states) * std::size(sizes)) ? 0 : 1;
}

// ---- By hand ----

int Manual() {
    input::Init();
    SDL_Window* window = SDL_CreateWindow("osktest --manual", 1280, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) return 1;
    SDL_SetRenderVSync(renderer, 1);
    if (const char* want = std::getenv("LBP3_DISPLAY")) {  // e.g. 165hz, as the game's launchers set it
        char* end = nullptr;
        const double hz = std::strtod(want, &end);
        int count = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&count);
        SDL_DisplayID pick = 0;
        double best = 1e9;
        for (int i = 0; ids && end && (*end == 'h' || *end == 'H') && i < count; ++i) {
            const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(ids[i]);
            if (m && std::fabs(m->refresh_rate - hz) < best) {
                best = std::fabs(m->refresh_rate - hz);
                pick = ids[i];
            }
        }
        if (pick) SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(pick), SDL_WINDOWPOS_CENTERED_DISPLAY(pick));
        SDL_free(ids);
    }
    Attach(window, renderer);
    Request req;
    req.title = u"Level Name";
    req.text = u"My level";
    req.maxLength = 64;
    bool quit = false;
    while (!quit) {
        if (!Visible()) {
            Show(req, [&](const Answer& a) {
                std::printf("%s: \"%s\"\n", a.confirmed ? "confirmed" : "cancelled", Utf8(a.text).c_str());
                if (a.confirmed) req.text = a.text;
            });
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit = true;
            HandleEvent(e);
        }
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(renderer, &w, &h);
        FakeGame(renderer, w, h);
        Draw();
        SDL_RenderPresent(renderer);
    }
    Attach(nullptr, nullptr);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL video init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--shot") == 0) return Shots(argv[2]);
    if (argc >= 2 && std::strcmp(argv[1], "--manual") == 0) return Manual();

    TablesChecks();
    TextChecks();
    NavigationChecks();
    ModeChecks();
    RepeaterChecks();
    WordsChecks();
    BarChecks();
    OverlayChecks();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    SDL_Quit();
    return g_failures == 0 ? 0 : 1;
}
