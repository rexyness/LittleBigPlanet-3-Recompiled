// The mouse as a finger on player 1's touch pad (see mouse_touch.h).
#include "input/mouse_touch.h"

#include "hle/modal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace input::mouse_touch {

void Touch::Down(uint64_t nowNs) {
    m_down = true;
    m_x = kWidth / 2;
    m_y = kHeight / 2;
    m_id = m_nextId;
    m_nextId = static_cast<uint8_t>((m_nextId + 1) & 0x7f);
    m_lastMoveNs = nowNs;
}

void Touch::Move(float dx, float dy, uint64_t nowNs) {
    if (m_gesture) return;  // the wheel's two fingers are on the pad
    if (nowNs < m_liftedUntilNs) return;  // lifted near an edge: the game must see it up first
    if (!m_down) Down(nowNs);
    m_x += dx;
    m_y += dy;
    m_lastMoveNs = nowNs;
    if (m_x < kEdgeMargin || m_x > kWidth - kEdgeMargin || m_y < kEdgeMargin || m_y > kHeight - kEdgeMargin) {
        // Lift, and go down at the centre on the next movement (touch input is relative).
        m_x = std::clamp(m_x, 0.f, kWidth - 1);
        m_y = std::clamp(m_y, 0.f, kHeight - 1);
        m_down = false;
        m_liftedUntilNs = nowNs + kLiftedForNs;
    }
}

void Touch::Button(bool down, uint64_t nowNs) {
    m_button = down;
    if (down && !m_down && !m_gesture) Down(nowNs);  // pressing the pad in puts a finger on it
    if (down) m_lastMoveNs = nowNs;
}

void Touch::Wheel(float notches, bool twist, uint64_t nowNs) {
    if (notches == 0.f) return;
    if (!m_gesture) {
        m_gesture = true;
        m_down = false;  // the sliding finger leaves
        m_sep = m_sepTarget = kGestureStart;
        m_angle = m_angleTarget = 0.f;
        m_gestureId = m_nextId;
        m_nextId = static_cast<uint8_t>((m_nextId + 2) & 0x7f);
        m_lastFillNs = nowNs;
    }
    if (twist)
        m_angleTarget += notches * kTwistStep;
    else
        m_sepTarget = std::clamp(m_sepTarget + notches * kGestureStep, 60.f, kHeight - 2 * kEdgeMargin);
    m_lastWheelNs = nowNs;
}

void Touch::Release() {
    m_down = false;
    m_button = false;
    m_gesture = false;
}

void Touch::Fill(ps4::PadData& out, uint64_t nowNs) {
    if (m_gesture) {
        const float dt = std::clamp((nowNs - m_lastFillNs) / 1e9f, 0.f, 0.1f);
        m_lastFillNs = nowNs;
        auto approach = [](float v, float target, float step) {
            return std::fabs(target - v) <= step ? target : v + (target > v ? step : -step);
        };
        m_sep = approach(m_sep, m_sepTarget, kGestureSpeed * dt);
        m_angle = approach(m_angle, m_angleTarget, kTwistSpeed * dt);
        const bool settled = m_sep == m_sepTarget && m_angle == m_angleTarget;
        if (settled && nowNs - m_lastWheelNs > kGestureEndNs) {
            m_gesture = false;  // both fingers lift this frame
        } else {
            const float cx = kWidth / 2, cy = kHeight / 2;
            const float ox = std::cos(m_angle) * m_sep / 2, oy = std::sin(m_angle) * m_sep / 2;
            const float pts[2][2] = {{cx - ox, cy - oy}, {cx + ox, cy + oy}};
            out.touchData.touchNum = 2;
            for (int i = 0; i < 2; ++i) {
                ps4::PadTouch& t = out.touchData.touch[i];
                t.x = static_cast<uint16_t>(std::clamp(pts[i][0], 0.f, kWidth - 1));
                t.y = static_cast<uint16_t>(std::clamp(pts[i][1], 0.f, kHeight - 1));
                t.id = static_cast<uint8_t>((m_gestureId + i) & 0x7f);
            }
            if (m_button) out.buttons |= ps4::PAD_TOUCH_PAD;
            return;
        }
    }
    if (m_down && !m_button && nowNs - m_lastMoveNs > kLiftAfterNs) m_down = false;  // still: lift
    if (m_button) out.buttons |= ps4::PAD_TOUCH_PAD;
    if (!m_down) return;
    out.touchData.touchNum = 1;
    ps4::PadTouch& t = out.touchData.touch[0];
    t.x = static_cast<uint16_t>(std::clamp(m_x, 0.f, kWidth - 1));
    t.y = static_cast<uint16_t>(std::clamp(m_y, 0.f, kHeight - 1));
    t.id = m_id;
}

namespace {

std::mutex g_mutex;
Touch g_touch;
std::atomic<bool> g_captured{false};
float g_speed = 1.f;

uint64_t NowNs() { return SDL_GetTicksNS(); }

void SetCaptured(SDL_Window* window, bool on) {
    if (window) SDL_SetWindowRelativeMouseMode(window, on);
    g_captured = on;
    if (!on) {
        std::lock_guard lock(g_mutex);
        g_touch.Release();
    }
    std::fprintf(stderr, "[mouse] touch pad mode %s (F7 toggles)\n", on ? "on: the mouse is the touch pad" : "off");
}

}  // namespace

bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("LBP3_MOUSE_TOUCH");
        if (const char* s = std::getenv("LBP3_MOUSE_TOUCH_SPEED")) g_speed = std::clamp(static_cast<float>(std::atof(s)), 0.1f, 10.f);
        return v && std::strcmp(v, "1") == 0;
    }();
    return on;
}

bool HandleEvent(const SDL_Event& e) {
    if (!Enabled()) return false;
    SDL_Window* window = nullptr;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F7) {
        window = SDL_GetWindowFromID(e.key.windowID);
        SetCaptured(window, !g_captured.load());
        return true;
    }
    if (!g_captured.load()) return false;
    if (hle::modal::Active()) {
        // A system dialog or the on-screen keyboard: give the mouse back to it.
        SDL_Window* w = SDL_GetMouseFocus();
        if (!w) w = SDL_GetKeyboardFocus();
        SetCaptured(w, false);
        return false;
    }
    std::lock_guard lock(g_mutex);
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        g_touch.Move(e.motion.xrel * g_speed, e.motion.yrel * g_speed, NowNs());
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        g_touch.Button(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, NowNs());
        return true;
    case SDL_EVENT_MOUSE_WHEEL: {
        const bool twist = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
        const float notches = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
        g_touch.Wheel(notches, twist, NowNs());
        return true;
    }
    default: return false;
    }
}

void RemoteCapture(bool on) {
    if (!Enabled()) return;
    SetCaptured(nullptr, on);
}

void RemoteMove(float dx, float dy) {
    std::lock_guard lock(g_mutex);
    if (g_captured.load()) g_touch.Move(dx, dy, NowNs());
}

void RemoteButton(bool down) {
    std::lock_guard lock(g_mutex);
    if (g_captured.load()) g_touch.Button(down, NowNs());
}

void RemoteWheel(float notches, bool twist) {
    std::lock_guard lock(g_mutex);
    if (g_captured.load()) g_touch.Wheel(notches, twist, NowNs());
}

void Apply(ps4::PadData& out) {
    if (!Enabled()) return;
    if (out.touchData.touchNum > 0) return;  // a real touch pad is being touched
    std::lock_guard lock(g_mutex);
    if (!g_captured.load()) return;
    g_touch.Fill(out, NowNs());
}

}  // namespace input::mouse_touch
