// Local players across the HLE libraries. libSceUserService (hle/system_services.cpp) signs in
// user 1 always and users 2-4 while their controller is connected (input/gamepad.h: user N plays
// with pad slot N-1).
#pragma once
#include <cstdint>

namespace hle::np {

// A user signed in: libSceNpManager reports their (offline) PSN state to the game's state
// callback on its next sceNpCheckCallback, as it does for user 1 after the callback is
// registered. Any thread.
void QueueStateNotice(int32_t userId);
// A user signed out: a notice not yet delivered is dropped. Any thread.
void DropStateNotice(int32_t userId);
// Runs at the start of every sceNpCheckCallback, on the game's thread (shadNet's push events and
// invitations, hle/np_shadnet.cpp). Unset (and nothing extra runs) unless shadNet is on.
void SetCheckCallbackHook(void (*hook)());

}  // namespace hle::np
