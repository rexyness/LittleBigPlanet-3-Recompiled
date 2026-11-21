// Quitting the game from inside it (best-of-both-worlds branch): the in-game menu's "Quit game"
// and "Quit Game" in the game's own pause menu.
//
// RequestQuit closes the game the way the window's close button does (the presenter's quit path:
// "[video] window closed", then the process ends), but first has the game save what the player
// hasn't saved yet, then waits until no save data is mounted. LBP3 saves a profile only when its
// "save requested" flag is set, and a Pod edit (a sticker, an object) sets only "Pod changed",
// which the game's per-frame autosave ignores: quitting from the Pod lost every edit since the
// last save. RequestQuit does what the script native ProcessDirty__ (eboot+0x4f30c0) does, setting
// "save requested" on each signed-in player's profiles that have unsaved changes, and the game's
// own per-frame save update (eboot+0x4f3620) then saves them on the game thread; a profile save
// made in the Pod takes the live Pod with it (eboot+0x260210). It then waits for the profiles to
// be clean, the save queue empty and nothing mounted. If the game doesn't save within 20 s, or
// stops drawing frames (the attract video), it closes anyway and says so. The window's close
// button goes the same way. A second close while it saves stops waiting for the game, but a save
// being written still finishes first (saves are written in place: closing mid-write could leave
// one half-written).
//
// The pause menu (LBP3_QUIT_MENU=1, with LBP3_FILE_EDITS pointing at tools/quit_menu.py's
// description): the script edit makes the menu's Good Grief! button (online-only, greyed out
// offline) ask the script native LAMS.Translate (Translate__i, eboot+0x839aa0) for two keys of
// ours, which the hook here answers: kQuitLabel is the button's text ("Quit Game", or "Press
// again to quit" for 3 s after a first press) and kQuitAction a press. The first press arms it,
// the second quits. The approach, the two keys and the three-second confirmation are LBPPC's
// (emurphy04, aad6a91: scripts/quit_menu.py, GPL-2.0-or-later); this hook is our own, on our
// script-native binding table.
#pragma once
#include <cstdint>

namespace hle::quit {

constexpr int32_t kQuitLabel = 0x4C425051;   // 'LBPQ'
constexpr int32_t kQuitAction = 0x4C425058;  // 'LBPX'

// Any thread. Only the first call does anything.
void RequestQuit(const char* why);
bool QuitRequested();

// The presenter, on the window's close request: true = close now (the save-first quit has finished,
// or this is a second close while it runs and nothing is being written); false = a save-first quit
// has started instead, or (a second close) it will close once the save being written is done.
bool CloseWindowNow();

// One close request's decision, for the test: `approved` = the save-first quit has finished,
// `requested` = one is running, `mounted` = save data is mounted (a save is being written).
enum class CloseStep { Now, SaveFirst, AfterWrite };
CloseStep DecideClose(bool approved, bool requested, bool mounted);

// The game's profile-save state at one moment (read from its memory; SaveStateNow in quit.cpp).
struct SaveState {
    bool known = false;  // this game version's save data was recognised
    int profiles = 0;    // the signed-in players' profiles (each player's little and big profile)
    int dirty = 0;       // with unsaved changes (flags 0x01 changed, 0x40 Pod changed, 0x80)
    int pending = 0;     // asked to save or being saved (0x02 requested, 0x04 in flight)
    int queued = 0;      // operations in the save manager's queue
    bool mounted = false;  // any /savedataN mounted
    int64_t frame = 0;     // the game's frame counter
};
enum class SaveStep { Wait, Request, Saved, NothingToSave, TimedOut, Stalled, Unknown };
constexpr uint64_t kSaveTimeoutMs = 20000;  // the game took about 6 s for a Pod save (2026-11-20)
constexpr uint64_t kStallMs = 3000;         // no new frame for this long: the world isn't updating

// One poll of the save-first quit, for the test: `start` is the state when the quit was asked
// for, `now` the current one, `elapsedMs` since then, `sinceFrameMs` since the frame counter last
// moved. Request = (re)ask the game for the save (dirty, nothing pending).
SaveStep DecideSave(const SaveState& start, const SaveState& now, uint64_t elapsedMs, uint64_t sinceFrameMs);

// LBP3_QUIT_MENU=1: hooks Translate__i (after the executable is linked, before the game runs).
void InstallPauseMenuQuit();

// The pause-menu button's logic, for the test: the text for `key` at `nowMs`, given when a
// first press armed it (0 = not armed). For kQuitAction, `pressArmedUntil` is updated and
// `quit` set on the confirming press. Other keys return nullptr (the game's own text).
const char16_t* PauseMenuText(int32_t key, uint64_t nowMs, uint64_t& armedUntil, bool& quit);

}  // namespace hle::quit
