// The in-game menu's items and navigation (ui/menu_overlay.h draws it), without ImGui, so the
// test (tests/uxtest.cpp) can drive it. The idea of an in-game menu on L3+R3 / Insert with
// stats and toggles is LBPPC's (emurphy04, aad6a91); this is our own implementation.
#pragma once
#include <cstdint>

namespace ui::menu {

enum class Item : int { Stats, FrameGen, Volume, Unlocks, Quit, Close, kCount };
enum class Action : int { None, ToggleStats, ToggleFrameGen, Volume, ArmUnlocks, CancelUnlocks, QuitArmed, Quit, Close };
// "Import LBP1/LBP2 items" (hle/my_unlocks.h; the same order as its State).
enum class Unlocks : int { Unavailable, Idle, Armed, Running, Done };

struct Model {
    int selected = 0;
    bool stats = false;        // the corner stats overlay
    bool frameGenAvailable = false;
    bool frameGenOn = false;
    int volume = 100;          // percent, steps of 10
    uint64_t quitArmedUntil = 0;
    Unlocks unlocks = Unlocks::Unavailable;

    bool Enabled(Item i) const {
        if (i == Item::FrameGen) return frameGenAvailable;
        if (i == Item::Unlocks) return unlocks != Unlocks::Unavailable && unlocks != Unlocks::Running;
        return true;
    }
    Item Selected() const { return static_cast<Item>(selected); }
    bool QuitArmed(uint64_t nowMs) const { return quitArmedUntil > nowMs; }

    // Up/down: the previous/next enabled item (wraps).
    void Move(int delta);
    // Left/right on the selected item: volume +-10, toggles flip.
    Action Adjust(int delta);
    // Cross/Enter on the selected item. "Quit game" needs a second press within 3 s. The import
    // item arms the import (idle or done) or disarms it (armed, not started yet).
    Action Activate(uint64_t nowMs);
};

}  // namespace ui::menu
