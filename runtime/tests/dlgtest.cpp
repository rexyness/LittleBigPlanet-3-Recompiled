// Offline checks for the system dialogs drawn over the game (ui/dialog_overlay) and the
// overlay host they share with the on-screen keyboard (ui/overlay). Drives them through the
// host as the presenter does (Attach, HandleEvent, Draw) on a hidden window, with a scripted
// pad, clock, keys and mouse. Prints each failed check; exit code 0 = all passed.
//
//   dlgtest               the checks
//   dlgtest --shot <dir>  PNGs of each kind of dialog over a fake game frame, at 1280x720 and
//                         1920x1080 (offscreen; no window is shown). <dir>\icon_sample.png, if
//                         present, is the list's sample save icon.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "hle/pad_types.h"
#include "ui/dialog_overlay.h"
#include "ui/dialog_testing.h"
#include "ui/osk_overlay.h"
#include "ui/osk_testing.h"
#include "ui/overlay.h"

using namespace ui::dialog;

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

uint64_t g_now = 1000;
uint64_t Clock() { return g_now; }

ps4::PadData g_pad{};
SDL_Window* g_window = nullptr;

// One presenter frame with the pad in this state.
void Pad(uint32_t buttons, int sx = 0, int sy = 0) {
    g_pad.buttons = buttons;
    g_pad.leftStick = {uint8_t(128 + sx), uint8_t(128 + sy)};
    g_now += 16;
    ui::overlay::Draw();
}

struct Answers {
    int count = 0;
    int button = 0;
    int item = -2;
};

AnswerFn Recorder(Answers& a) {
    return [&a](const ui::dialog::Answer& ans) {
        ++a.count;
        a.button = ans.button;
        a.item = ans.item;
    };
}

testing::State Top() {
    testing::State s;
    testing::Top(s);
    return s;
}

SDL_Event KeyEvent(SDL_EventType type, SDL_Keycode key, bool repeat = false) {
    SDL_Event e{};
    e.type = type;
    e.key.key = key;
    e.key.repeat = repeat;
    return e;
}

void Key(SDL_Keycode key, bool repeat = false) {
    ui::overlay::HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, key, repeat));
    ui::overlay::HandleEvent(KeyEvent(SDL_EVENT_KEY_UP, key));
    Pad(0);
}

// The mouse, as SDL reports it to the game window: move, then (optionally) click.
void MouseMove(float x, float y) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(g_window);
    e.motion.x = x;
    e.motion.y = y;
    ui::overlay::HandleEvent(e);
    Pad(0);
}

void MouseClick(float x, float y) {
    MouseMove(x, y);
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.windowID = SDL_GetWindowID(g_window);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = true;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    ui::overlay::HandleEvent(e);
    Pad(0);
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.down = false;
    ui::overlay::HandleEvent(e);
    Pad(0);
}

Request YesNo() {
    Request r;
    r.kind = Kind::Message;
    r.icon = Icon::Warning;
    r.text = L"Do you want to overwrite this saved data?";
    r.button1 = L"Yes";
    r.button2 = L"No";
    r.defaultButton = 2;
    r.cancelButton = 2;
    r.logName = "savedata";
    return r;
}

Request OkOnly() {
    Request r;
    r.kind = Kind::Message;
    r.icon = Icon::Info;
    r.text = L"The saved data has been copied.";
    r.button1 = L"OK";
    r.defaultButton = 1;
    r.cancelButton = 1;
    r.logName = "msg";
    return r;
}

Request SaveList(int entries, int focus) {
    Request r;
    r.kind = Kind::List;
    r.icon = Icon::None;
    r.text = L"Choose where to save.";
    r.button1 = L"Save";
    r.button2 = L"Cancel";
    r.cancelButton = 2;
    r.focus = focus;
    r.logName = "savedata";
    for (int i = 0; i < entries; ++i) {
        wchar_t label[96];
        std::swprintf(label, 96, L"CUSA00473x00LEVEL%08X    2026-11-%02d 16:%02d", 0x6AF46617u + i, 1 + i % 28, i % 60);
        r.items.push_back({label, L""});
    }
    return r;
}

void MessageChecks() {
    std::printf("message\n");
    // Cross held from the game's menu when the dialog opens answers nothing.
    Answers a1;
    Pad(ps4::PAD_CROSS);
    const uint64_t id1 = Show(YesNo(), Recorder(a1));
    CHECK(id1 != 0);
    CHECK(Visible());
    Pad(ps4::PAD_CROSS);
    Pad(ps4::PAD_CROSS);
    CHECK_EQ(a1.count, 0);
    CHECK_EQ(Top().focus, 2);  // the default button, No
    Pad(0);
    Pad(ps4::PAD_LEFT);  // to Yes
    CHECK_EQ(Top().focus, 1);
    Pad(0);
    Pad(ps4::PAD_RIGHT);  // back to No
    CHECK_EQ(Top().focus, 2);
    Pad(0);
    Pad(ps4::PAD_UP);  // up is left too
    CHECK_EQ(Top().focus, 1);
    Pad(0);
    Pad(0, -100, 0);  // the stick: left stays on button 1, right is button 2
    Pad(0);
    Pad(0, 100, 0);
    CHECK_EQ(Top().focus, 2);
    Pad(0);
    Pad(0, -100, 0);
    CHECK_EQ(Top().focus, 1);
    Pad(0);
    Pad(ps4::PAD_CROSS);  // Cross presses the focused button
    CHECK_EQ(a1.count, 1);
    CHECK_EQ(a1.button, 1);
    CHECK_EQ(a1.item, -1);
    CHECK(!Visible());
    CHECK_EQ(testing::LiveCount(), size_t(0));
    Close(id1);  // stale: ignored
    Pad(0);
    CHECK_EQ(a1.count, 1);

    // Circle does what cancelButton says.
    Answers a2;
    Show(YesNo(), Recorder(a2));
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(a2.count, 1);
    CHECK_EQ(a2.button, 2);
    Pad(0);
    Answers a3;
    Request dismiss = YesNo();
    dismiss.cancelButton = kDismiss;
    Show(dismiss, Recorder(a3));
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(a3.count, 1);
    CHECK_EQ(a3.button, kDismiss);
    Pad(0);
    // cancelButton 0: Circle and Esc do nothing; Enter presses the focused button.
    Answers a4;
    Request stuck = YesNo();
    stuck.cancelButton = 0;
    Show(stuck, Recorder(a4));
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    Key(SDLK_ESCAPE);
    CHECK_EQ(a4.count, 0);
    CHECK(Visible());
    Key(SDLK_RETURN, true);  // a repeat (held from the game) is not an answer
    CHECK_EQ(a4.count, 0);
    Key(SDLK_LEFT);
    Key(SDLK_RETURN);
    CHECK_EQ(a4.count, 1);
    CHECK_EQ(a4.button, 1);

    // OK only: Cross and Circle both give button 1; a default that isn't there falls on OK.
    Answers a5;
    Request ok = OkOnly();
    ok.defaultButton = 2;
    Show(ok, Recorder(a5));
    Pad(0);
    CHECK_EQ(Top().focus, 1);
    Pad(ps4::PAD_RIGHT);  // nothing to move to
    CHECK_EQ(Top().focus, 1);
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(a5.count, 1);
    CHECK_EQ(a5.button, 1);
    Pad(0);

    // Keys: Esc cancels, Tab and the arrows move the focus, Space presses.
    Answers a6;
    Show(YesNo(), Recorder(a6));
    Pad(0);
    Key(SDLK_TAB);
    CHECK_EQ(Top().focus, 1);
    Key(SDLK_ESCAPE);
    CHECK_EQ(a6.count, 1);
    CHECK_EQ(a6.button, 2);
    Answers a7;
    Show(YesNo(), Recorder(a7));
    Pad(0);
    Key(SDLK_UP);
    Key(SDLK_SPACE);
    CHECK_EQ(a7.count, 1);
    CHECK_EQ(a7.button, 1);

    // The touch-pad click is Cross.
    Answers a8;
    Show(OkOnly(), Recorder(a8));
    Pad(0);
    Pad(ps4::PAD_TOUCH_PAD);
    CHECK_EQ(a8.count, 1);
    Pad(0);
}

void ListChecks() {
    std::printf("list\n");
    Answers a1;
    Show(SaveList(4, 1), Recorder(a1));
    Pad(0);
    CHECK_EQ(Top().selected, 1);
    CHECK_EQ(Top().focus, 1);  // Cross chooses the selected entry
    Pad(ps4::PAD_DOWN);
    CHECK_EQ(Top().selected, 2);
    Pad(ps4::PAD_DOWN);  // +16 ms: no repeat yet
    CHECK_EQ(Top().selected, 2);
    g_now += 400;
    Pad(ps4::PAD_DOWN);  // held: repeats
    CHECK_EQ(Top().selected, 3);
    g_now += 100;
    Pad(ps4::PAD_DOWN);  // the end: stays (no wrap, as the native list)
    CHECK_EQ(Top().selected, 3);
    Pad(0);
    Pad(ps4::PAD_LEFT);  // left and right do nothing in a list
    CHECK_EQ(Top().selected, 3);
    Pad(0);
    Pad(ps4::PAD_UP);
    CHECK_EQ(Top().selected, 2);
    Pad(0);
    Pad(ps4::PAD_CROSS);
    CHECK_EQ(a1.count, 1);
    CHECK_EQ(a1.button, 1);
    CHECK_EQ(a1.item, 2);
    Pad(0);
    // Circle: button 2, no entry.
    Answers a2;
    Show(SaveList(4, 0), Recorder(a2));
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(a2.count, 1);
    CHECK_EQ(a2.button, 2);
    CHECK_EQ(a2.item, -1);
    Pad(0);
    // A long list scrolls to keep the selection in view; keys move it too.
    Answers a3;
    Show(SaveList(12, 9), Recorder(a3));
    Pad(0);
    CHECK_EQ(Top().selected, 9);
    CHECK_EQ(Top().firstRow, 4);  // 6 rows visible: 4..9
    Key(SDLK_HOME);
    CHECK_EQ(Top().selected, 0);
    CHECK_EQ(Top().firstRow, 0);
    Key(SDLK_PAGEDOWN);
    CHECK_EQ(Top().selected, 6);
    CHECK_EQ(Top().firstRow, 1);
    Key(SDLK_END);
    CHECK_EQ(Top().selected, 11);
    CHECK_EQ(Top().firstRow, 6);
    Key(SDLK_DOWN);
    Key(SDLK_UP);
    CHECK_EQ(Top().selected, 10);
    Key(SDLK_RETURN);
    CHECK_EQ(a3.count, 1);
    CHECK_EQ(a3.item, 10);
    // A focus past the end is clamped; an empty list answers nothing to Cross.
    Answers a4;
    Show(SaveList(3, 7), Recorder(a4));
    Pad(0);
    CHECK_EQ(Top().selected, 2);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    Answers a5;
    Show(SaveList(0, 0), Recorder(a5));
    Pad(0);
    Pad(ps4::PAD_CROSS);
    CHECK_EQ(a5.count, 0);
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(a5.count, 1);
    CHECK_EQ(a5.button, 2);
    Pad(0);
}

void ProgressChecks() {
    std::printf("progress\n");
    Request r;
    r.kind = Kind::Progress;
    r.icon = Icon::None;
    r.text = L"Saving...";
    r.progress = 10;
    r.logName = "savedata";
    Answers a;
    const uint64_t id = Show(r, Recorder(a));
    SetProgress(id, 55);  // before the presenter picked it up
    Pad(0);
    CHECK_EQ(Top().progress, 55u);
    SetProgress(id, 140);  // clamped
    Pad(0);
    CHECK_EQ(Top().progress, 100u);
    Pad(ps4::PAD_CROSS);  // no buttons: nothing to press
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    Key(SDLK_ESCAPE);
    CHECK_EQ(a.count, 0);
    CHECK(Visible());
    Close(id);
    CHECK(!Visible());
    Pad(0);
    CHECK_EQ(testing::LiveCount(), size_t(0));
    CHECK_EQ(a.count, 0);
    SetProgress(id, 20);  // stale: ignored
    Pad(0);
}

void CloseAndPadIgnoredChecks() {
    std::printf("close, padIgnored\n");
    // Close before and after the presenter picked it up: no answer either way.
    Answers a1, a2;
    const uint64_t id1 = Show(YesNo(), Recorder(a1));
    Close(id1);
    Pad(0);
    CHECK_EQ(testing::LiveCount(), size_t(0));
    const uint64_t id2 = Show(YesNo(), Recorder(a2));
    Pad(0);
    CHECK_EQ(testing::LiveCount(), size_t(1));
    Close(id2);
    Pad(ps4::PAD_CROSS);
    CHECK_EQ(a1.count, 0);
    CHECK_EQ(a2.count, 0);
    CHECK_EQ(testing::LiveCount(), size_t(0));
    Pad(0);
    // padIgnored (LBP3_AUTOPRESS runs): the pad does nothing; the keys still work.
    Answers a3;
    Request r = YesNo();
    r.padIgnored = true;
    Show(r, Recorder(a3));
    Pad(0);
    Pad(ps4::PAD_CROSS);
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    Pad(0);
    Pad(ps4::PAD_LEFT);
    Pad(0);
    CHECK_EQ(a3.count, 0);
    CHECK_EQ(Top().focus, 2);
    Key(SDLK_RETURN);
    CHECK_EQ(a3.count, 1);
    CHECK_EQ(a3.button, 2);
}

void StackChecks() {
    std::printf("stacked\n");
    // Two dialogs: the newer takes the input; the Cross that answered it doesn't reach the older.
    Answers older, newer;
    const uint64_t idOld = Show(OkOnly(), Recorder(older));
    Pad(0);
    const uint64_t idNew = Show(YesNo(), Recorder(newer));
    Pad(0);
    CHECK_EQ(testing::LiveCount(), size_t(2));
    CHECK_EQ(Top().id, idNew);
    Pad(ps4::PAD_LEFT);
    Pad(ps4::PAD_LEFT | ps4::PAD_CROSS);
    CHECK_EQ(newer.count, 1);
    CHECK_EQ(newer.button, 1);
    CHECK_EQ(Top().id, idOld);
    Pad(ps4::PAD_LEFT | ps4::PAD_CROSS);  // still held
    Pad(ps4::PAD_CROSS);
    CHECK_EQ(older.count, 0);
    Pad(0);
    Pad(ps4::PAD_CROSS);
    CHECK_EQ(older.count, 1);
    Pad(0);
    // Closing the newer one by id leaves the older one up.
    Answers o2, n2;
    const uint64_t a = Show(OkOnly(), Recorder(o2));
    const uint64_t b = Show(YesNo(), Recorder(n2));
    Pad(0);
    Close(b);
    Pad(0);
    CHECK_EQ(testing::LiveCount(), size_t(1));
    CHECK_EQ(Top().id, a);
    Close(a);
    Pad(0);
    CHECK_EQ(o2.count + n2.count, 0);
}

void KeyboardTogetherChecks() {
    std::printf("keyboard and dialog\n");
    ui::osk::testing::SetPad(&g_pad);
    ui::osk::testing::SetClock(Clock);
    const auto text = [] {
        std::u16string t;
        ui::osk::testing::WithKeyboard([&](ui::osk::Keyboard& kb) { t = kb.text(); });
        return t;
    };
    // A dialog over the keyboard takes the pad; the keyboard types nothing meanwhile.
    ui::osk::Request kreq;
    kreq.title = u"Level Name";
    kreq.text = u"abc";
    kreq.maxLength = 20;
    int kAnswers = 0;
    std::u16string kText;
    const uint64_t kid = ui::osk::Show(kreq, [&](const ui::osk::Answer& a) {
        ++kAnswers;
        kText = a.text;
    });
    Pad(0);
    Answers d;
    Show(OkOnly(), Recorder(d));
    Pad(0);
    SDL_Event typed{};
    typed.type = SDL_EVENT_TEXT_INPUT;
    typed.text.text = "zz";
    CHECK(ui::overlay::HandleEvent(typed));  // the dialog takes it (and ignores it)
    Pad(ps4::PAD_TRIANGLE);  // space, if the keyboard had the pad
    Pad(0);
    CHECK(text() == u"abc");
    Pad(ps4::PAD_CROSS);  // answers the dialog
    CHECK_EQ(d.count, 1);
    Pad(ps4::PAD_CROSS);  // still held: not a key press on the keyboard now on top
    CHECK(text() == u"abc");
    Pad(0);
    Pad(ps4::PAD_CROSS);  // a fresh press types the selected key (q)
    CHECK(text() == u"abcq");
    Pad(0);
    // A keyboard shown after a dialog is on top of it.
    Answers d2;
    const uint64_t did = Show(YesNo(), Recorder(d2));
    Pad(0);
    ui::osk::Close(kid);
    Pad(0);
    const uint64_t kid2 = ui::osk::Show(kreq, [&](const ui::osk::Answer& a) {
        ++kAnswers;
        kText = a.text;
    });
    Pad(0);
    Pad(ps4::PAD_CROSS);
    CHECK(text() == u"abcq");
    CHECK_EQ(d2.count, 0);
    Pad(0);
    Pad(ps4::PAD_R2);  // the keyboard confirms; the dialog is back on top, R2 means nothing there
    CHECK_EQ(kAnswers, 1);
    CHECK(kText == u"abcq");
    Pad(0);
    Pad(ps4::PAD_CIRCLE);
    CHECK_EQ(d2.count, 1);
    CHECK_EQ(d2.button, 2);
    (void)did;
    (void)kid2;
    Pad(0);
    ui::osk::testing::SetPad(nullptr);
    ui::osk::testing::SetClock(nullptr);
}

void EventChecks() {
    std::printf("events\n");
    CHECK(!ui::overlay::HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_A)));  // nothing up: not ours
    Answers a;
    Show(YesNo(), Recorder(a));
    Pad(0);
    CHECK(ui::overlay::HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_A)));  // a dialog is up: not a game key
    CHECK(!ui::overlay::HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_F9)));  // a debug hotkey stays one
    CHECK(!ui::overlay::HandleEvent(KeyEvent(SDL_EVENT_KEY_UP, SDLK_F10)));
    Key(SDLK_ESCAPE);
    CHECK_EQ(a.count, 1);
}

void MouseChecks() {
    std::printf("mouse\n");
    float x = 0.f, y = 0.f;
    // A click on a button presses it.
    Answers a1;
    Show(YesNo(), Recorder(a1));
    Pad(0);
    Pad(0);
    CHECK(testing::ButtonCentre(1, x, y));
    MouseMove(x - 6.f, y);  // the first position seen only notes where the pointer is
    CHECK_EQ(Top().focus, 2);
    MouseMove(x, y);  // moving over a button focuses it
    CHECK_EQ(Top().focus, 1);
    MouseClick(x, y);
    CHECK_EQ(a1.count, 1);
    CHECK_EQ(a1.button, 1);
    // The next dialog's first click isn't swallowed by the last one's (review fix kept).
    Answers a2;
    Show(YesNo(), Recorder(a2));
    Pad(0);
    Pad(0);
    CHECK(testing::ButtonCentre(2, x, y));
    MouseClick(x, y);
    CHECK_EQ(a2.count, 1);
    CHECK_EQ(a2.button, 2);
    // A list: one click selects, a second on the same entry chooses it.
    Answers a3;
    Show(SaveList(5, 0), Recorder(a3));
    Pad(0);
    Pad(0);
    CHECK(testing::RowCentre(3, x, y));
    MouseClick(x, y);
    CHECK_EQ(Top().selected, 3);
    CHECK_EQ(a3.count, 0);
    MouseClick(x, y);
    CHECK_EQ(a3.count, 1);
    CHECK_EQ(a3.item, 3);
    // Clicks far apart in time are two single clicks; then Cross chooses.
    Answers a4;
    Show(SaveList(5, 0), Recorder(a4));
    Pad(0);
    Pad(0);
    CHECK(testing::RowCentre(2, x, y));
    MouseClick(x, y);
    g_now += 1000;
    MouseClick(x, y);
    CHECK_EQ(a4.count, 0);
    Pad(ps4::PAD_CROSS);
    CHECK_EQ(a4.count, 1);
    CHECK_EQ(a4.item, 2);
    Pad(0);
    // The Circle hint cancels.
    Answers a5;
    Show(YesNo(), Recorder(a5));
    Pad(0);
    Pad(0);
    CHECK(testing::BackHintCentre(x, y));
    MouseClick(x, y);
    CHECK_EQ(a5.count, 1);
    CHECK_EQ(a5.button, 2);
    MouseMove(5.f, 5.f);
}

// Review fixes: a Space that answers a dialog types nothing into the keyboard under it, and a
// change of window brings the dialog and the keyboard back up instead of dropping them.
void ReviewFixChecks(SDL_Renderer* renderer) {
    std::printf("review fixes\n");
    ui::osk::testing::SetPad(&g_pad);
    ui::osk::testing::SetClock(Clock);
    const auto text = [] {
        std::u16string t;
        ui::osk::testing::WithKeyboard([&](ui::osk::Keyboard& kb) { t = kb.text(); });
        return t;
    };
    const auto key = [](SDL_EventType type, SDL_Keycode k, SDL_Scancode sc) {
        SDL_Event e = KeyEvent(type, k);
        e.key.scancode = sc;
        return ui::overlay::HandleEvent(e);
    };
    SDL_Event space{};
    space.type = SDL_EVENT_TEXT_INPUT;
    space.text.text = " ";

    ui::osk::Request kreq;
    kreq.title = u"Level Name";
    kreq.text = u"ab";
    kreq.maxLength = 20;
    int kAnswers = 0;
    std::u16string kText;
    const uint64_t kid = ui::osk::Show(kreq, [&](const ui::osk::Answer& a) {
        ++kAnswers;
        kText = a.text;
    });
    Pad(0);
    Answers d;
    Show(OkOnly(), Recorder(d));
    Pad(0);
    CHECK(key(SDL_EVENT_KEY_DOWN, SDLK_SPACE, SDL_SCANCODE_SPACE));  // answers the dialog
    CHECK_EQ(d.count, 1);
    CHECK(ui::overlay::HandleEvent(space));  // its text: swallowed, not typed
    CHECK(text() == u"ab");
    key(SDL_EVENT_KEY_UP, SDLK_SPACE, SDL_SCANCODE_SPACE);
    Pad(0);
    CHECK(ui::overlay::HandleEvent(space));  // a fresh space after the release types
    CHECK(text() == u"ab ");

    // A new window: the keyboard (with what was typed) and a dialog come back up on it.
    Answers d2;
    Show(SaveList(4, 2), Recorder(d2));
    Pad(0);
    Pad(ps4::PAD_DOWN);  // selection 2 -> 3
    Pad(0);
    SDL_Window* other = SDL_CreateWindow("dlgtest 2", 1280, 720, SDL_WINDOW_HIDDEN);
    SDL_Renderer* otherRenderer = other ? SDL_CreateRenderer(other, nullptr) : nullptr;
    CHECK(otherRenderer != nullptr);
    if (otherRenderer) {
        ui::overlay::Attach(other, otherRenderer);
        Pad(0);
        CHECK(Visible());
        CHECK_EQ(Top().selected, 3);  // the dialog came back where it was
        Pad(ps4::PAD_CROSS);
        CHECK_EQ(d2.count, 1);
        CHECK_EQ(d2.item, 3);
        Pad(0);
        CHECK(text() == u"ab ");  // the keyboard came back with its text
        Pad(ps4::PAD_R2);
        CHECK_EQ(kAnswers, 1);
        CHECK(kText == u"ab ");
        Pad(0);
        ui::overlay::Attach(g_window, renderer);
        SDL_DestroyRenderer(otherRenderer);
    }
    if (other) SDL_DestroyWindow(other);
    (void)kid;
    ui::osk::testing::SetPad(nullptr);
    ui::osk::testing::SetClock(nullptr);
}

void Checks() {
    g_window = SDL_CreateWindow("dlgtest", 1280, 720, SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = g_window ? SDL_CreateRenderer(g_window, nullptr) : nullptr;
    if (!renderer) {
        std::printf("no renderer (%s): checks skipped\n", SDL_GetError());
        CHECK(false);
        return;
    }
    CHECK(!Available());
    CHECK(!ui::osk::Available());
    ui::overlay::Attach(g_window, renderer);
    CHECK(Available());
    CHECK(ui::osk::Available());
    testing::SetClock(Clock);
    testing::SetPad(&g_pad);
    ui::overlay::Draw();  // nothing up: no-op
    CHECK(!Visible());

    MessageChecks();
    ListChecks();
    ProgressChecks();
    CloseAndPadIgnoredChecks();
    StackChecks();
    KeyboardTogetherChecks();
    EventChecks();
    MouseChecks();
    ReviewFixChecks(renderer);

    testing::SetPad(nullptr);
    testing::SetClock(nullptr);
    ui::overlay::Attach(nullptr, nullptr);
    CHECK(!Available());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
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

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // ASCII paths

int Shots(const char* dir) {
    const std::string iconPath = std::string(dir) + "\\icon_sample.png";
    FILE* f = std::fopen(iconPath.c_str(), "rb");
    const bool haveIcon = f != nullptr;
    if (f) std::fclose(f);
    if (!haveIcon) std::printf("no %s: the list has no icons\n", iconPath.c_str());

    struct State {
        const char* name;
        Request request;
        bool withKeyboard = false;
    };
    std::vector<State> states;
    {
        Request r = YesNo();
        r.text = L"Do you want to overwrite this saved data?\n\nSaved data: CUSA00473x00LEVEL6AF46617";
        states.push_back({"overwrite", r});
    }
    states.push_back({"info", OkOnly()});
    {
        Request r;
        r.icon = Icon::Error;
        r.text = L"There is not enough free space on the system storage to save this data. "
                 L"Delete other saved data or applications and try again.";
        r.button1 = L"OK";
        r.cancelButton = 1;
        states.push_back({"error", r});
    }
    {
        Request r;
        r.icon = Icon::Question;
        r.text = L"Delete this saved data?";
        r.button1 = L"Delete";
        r.button2 = L"Cancel";
        r.defaultButton = 2;
        r.cancelButton = 2;
        states.push_back({"question", r});
    }
    {
        Request r = SaveList(0, 1);
        r.items = {{L"[New saved data]  My first level", L""},
                   {L"CUSA00473x00LEVEL6AF46617    2026-11-11 16:44", haveIcon ? Wide(iconPath) : L""},
                   {L"CUSA00473x00LEVEL6AF4716E    2026-11-11 17:20", L""},
                   {L"LBPxSAVExLITTLE    2026-11-10 22:05", haveIcon ? Wide(iconPath) : L""}};
        states.push_back({"list", r});
    }
    {
        Request r = SaveList(12, 8);
        r.kind = Kind::List;
        r.text = L"Choose the saved data to load.";
        r.button1 = L"Load";
        states.push_back({"longlist", r});
    }
    {
        Request r;
        r.kind = Kind::Progress;
        r.icon = Icon::None;
        r.text = L"Saving...";
        r.progress = 40;
        states.push_back({"progress", r});
    }
    states.push_back({"over_keyboard", YesNo(), true});

    const int sizes[][2] = {{1280, 720}, {1920, 1080}};
    int written = 0;
    for (const auto& sz : sizes) {
        SDL_Window* window = SDL_CreateWindow("dlgtest shot", sz[0], sz[1], SDL_WINDOW_HIDDEN);
        SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
        if (!renderer) {
            std::printf("no renderer: %s\n", SDL_GetError());
            return 1;
        }
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pw, ph);
        ui::overlay::Attach(window, renderer);
        static ps4::PadData idle{};
        idle.leftStick = idle.rightStick = {128, 128};
        testing::SetPad(&idle);
        testing::SetClock(Clock);
        ui::osk::testing::SetPad(&idle);
        ui::osk::testing::SetClock(Clock);
        for (const State& st : states) {
            uint64_t kid = 0;
            if (st.withKeyboard) {
                ui::osk::Request k;
                k.title = u"Level Name";
                k.text = u"My first level";
                k.maxLength = 64;
                kid = ui::osk::Show(k, nullptr);
                ui::overlay::Draw();
            }
            const uint64_t id = Show(st.request, nullptr);
            ui::overlay::Draw();  // the presenter picks it up
            g_now += 16;
            SDL_SetRenderTarget(renderer, target);
            FakeGame(renderer, pw, ph);
            ui::overlay::Draw();
            SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
            SDL_SetRenderTarget(renderer, nullptr);
            char path[512];
            std::snprintf(path, sizeof(path), "%s\\dlg_%dx%d_%s.png", dir, pw, ph, st.name);
            if (shot && SDL_SavePNG(shot, path)) {
                std::printf("wrote %s\n", path);
                ++written;
            } else {
                std::printf("couldn't write %s: %s\n", path, SDL_GetError());
            }
            if (shot) SDL_DestroySurface(shot);
            Close(id);
            if (kid) ui::osk::Close(kid);
            ui::overlay::Draw();
        }
        testing::SetPad(nullptr);
        ui::osk::testing::SetPad(nullptr);
        ui::overlay::Attach(nullptr, nullptr);
        SDL_DestroyTexture(target);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
    return written == int(states.size() * std::size(sizes)) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL video init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--shot") == 0) return Shots(argv[2]);
    Checks();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    SDL_Quit();
    return g_failures == 0 ? 0 : 1;
}
