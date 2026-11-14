// The in-game menu's items and navigation. See menu_model.h.
#include "ui/menu_model.h"

#include <algorithm>

namespace ui::menu {

void Model::Move(int delta) {
    const int n = static_cast<int>(Item::kCount);
    int i = selected;
    for (int step = 0; step < n; ++step) {
        i = ((i + delta) % n + n) % n;
        if (Enabled(static_cast<Item>(i))) {
            selected = i;
            return;
        }
    }
}

Action Model::Adjust(int delta) {
    switch (Selected()) {
    case Item::Stats: stats = !stats; return Action::ToggleStats;
    case Item::FrameGen:
        if (!frameGenAvailable) return Action::None;
        frameGenOn = !frameGenOn;
        return Action::ToggleFrameGen;
    case Item::Volume: {
        const int v = std::clamp(volume + (delta > 0 ? 10 : -10), 0, 100);
        if (v == volume) return Action::None;
        volume = v;
        return Action::Volume;
    }
    default: return Action::None;
    }
}

Action Model::Activate(uint64_t nowMs) {
    switch (Selected()) {
    case Item::Stats:
    case Item::FrameGen: return Adjust(1);
    case Item::Volume: return Action::None;  // left/right change it
    case Item::Unlocks:
        if (unlocks == Unlocks::Idle || unlocks == Unlocks::Done) {
            unlocks = Unlocks::Armed;
            return Action::ArmUnlocks;
        }
        if (unlocks == Unlocks::Armed) {
            unlocks = Unlocks::Idle;
            return Action::CancelUnlocks;
        }
        return Action::None;
    case Item::Quit:
        if (QuitArmed(nowMs)) return Action::Quit;
        quitArmedUntil = nowMs + 3000;
        return Action::QuitArmed;
    case Item::Close: return Action::Close;
    default: return Action::None;
    }
}

}  // namespace ui::menu
