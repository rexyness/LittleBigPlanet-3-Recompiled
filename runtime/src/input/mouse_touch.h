// The mouse as a finger on player 1's touch pad (LBP3_MOUSE_TOUCH=1), for LBP3's touch Create
// mode and sticker placement without a DualShock 4 / DualSense touch pad. F7 captures the mouse
// (relative mode: the cursor is hidden and every movement goes to the touch pad); F7 again, or a
// system dialog or the on-screen keyboard opening, releases it.
//  - Move: one finger slides, relative to where it is. It goes down at the pad's centre, lifts
//    after 0.3 s without movement, and near an edge lifts and goes down at the centre again, so
//    the game never sees a jump.
//  - Left button: the touch pad pressed in (with the finger on the pad).
//  - Wheel: two fingers spread (up) or pinch (down), LBP3's resize gesture; Ctrl + wheel twists
//    them (rotate).
// Written for this port; the gestures follow LBPPC's description of its touch Create support
// (docs/input-research.md). No code is taken from it.
#pragma once
#include <cstdint>

#include "hle/pad_types.h"

union SDL_Event;

namespace input::mouse_touch {

// Touch pad units (DualShock 4 resolution, what the game expects).
constexpr float kWidth = 1920.f, kHeight = 943.f;
constexpr float kEdgeMargin = 64.f;           // closer than this to an edge: lift and re-centre
constexpr uint64_t kLiftAfterNs = 300'000'000;  // still this long: the finger lifts
constexpr uint64_t kLiftedForNs = 50'000'000;   // a lift lasts at least this long (a frame or two)
constexpr float kGestureStart = 400.f;        // the two fingers' starting separation
constexpr float kGestureStep = 120.f;         // separation change per wheel notch
constexpr float kTwistStep = 0.26f;           // radians per Ctrl + wheel notch (15 degrees)
constexpr float kGestureSpeed = 1500.f;       // units/s the fingers move toward their target
constexpr float kTwistSpeed = 3.f;            // rad/s
constexpr uint64_t kGestureEndNs = 300'000'000;  // the gesture ends this long after the last notch

// The touch state machine (no SDL; the tests drive it with a clock).
class Touch {
public:
    void Move(float dx, float dy, uint64_t nowNs);
    void Button(bool down, uint64_t nowNs);
    void Wheel(float notches, bool twist, uint64_t nowNs);
    // Fills touch data and the touch pad button for the frame at `nowNs`. Leaves `out` alone when
    // nothing is touching.
    void Fill(ps4::PadData& out, uint64_t nowNs);
    void Release();  // everything up (capture ended)

private:
    void Down(uint64_t nowNs);
    // One finger
    bool m_down = false;
    float m_x = kWidth / 2, m_y = kHeight / 2;
    uint64_t m_lastMoveNs = 0;
    uint64_t m_liftedUntilNs = 0;
    uint8_t m_id = 0, m_nextId = 0;
    bool m_button = false;
    // Two-finger gesture
    bool m_gesture = false;
    float m_sep = kGestureStart, m_sepTarget = kGestureStart;
    float m_angle = 0.f, m_angleTarget = 0.f;
    uint64_t m_lastWheelNs = 0, m_lastFillNs = 0;
    uint8_t m_gestureId = 0;
};

// LBP3_MOUSE_TOUCH=1.
bool Enabled();
// Window events, on the presenter's thread, after the overlays had theirs. True if used.
bool HandleEvent(const SDL_Event& e);
// Player 1's frame: the mouse's touches and touch pad press while captured (or still lifting).
// A real touch pad's touches take precedence.
void Apply(ps4::PadData& out);
// Test runs (remote input "touch ..."): the same as the mouse, without a window or a cursor.
void RemoteCapture(bool on);
void RemoteMove(float dx, float dy);
void RemoteButton(bool down);
void RemoteWheel(float notches, bool twist);

}  // namespace input::mouse_touch
