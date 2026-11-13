// Motion and the mouse as the touch pad, offline (input/motion.h, input/mouse_touch.h):
// the gyro offset learned while still, no drift over a minute idle, rotations tracked, tilt
// pulled back toward the measured gravity, the virtual tilt, and the mouse's touch gestures.
//   motiontest
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "hle/pad_types.h"
#include "input/motion.h"
#include "input/mouse_touch.h"

using namespace input::motion;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failed;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
static float Deg(float rad) { return rad * 57.29578f; }
// Angle of the rotation q (degrees).
static float AngleOf(const Quat& q) { return Deg(2.f * std::acos(std::fmin(1.f, std::fabs(q.w)))); }
static Quat AxisAngle(Vec3 axis, float angle) {
    const float s = std::sin(angle / 2);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle / 2)};
}
// Deterministic small noise.
static unsigned g_seed = 12345;
static float Noise(float amp) {
    g_seed = g_seed * 1103515245u + 12345u;
    return ((g_seed >> 8) % 2001 / 1000.f - 1.f) * amp;
}

int main() {
    const float dt = 1.f / 30.f;  // the game reads the pad once a frame
    const Vec3 up{0.f, 1.f, 0.f};

    // A fresh filter (no sensors): identity, +1 G on Y, no rotation.
    {
        Filter f;
        const Quat q = f.Orientation();
        CHECK(q.x == 0.f && q.y == 0.f && q.z == 0.f && q.w == 1.f);
        const Vec3 a = f.Acceleration();
        CHECK(a.x == 0.f && a.y == 1.f && a.z == 0.f);
        CHECK(!f.HasSensors());
    }

    // A pad lying still with a gyro offset: the offset is learned after half a second, the
    // reported rate is 0, and a minute later the orientation hasn't drifted.
    {
        Filter f;
        const Vec3 offset{0.02f, -0.015f, 0.01f};
        float t = 0.f;
        for (; t < 0.4f; t += dt) f.Sample({Noise(0.005f), 1.f + Noise(0.005f), Noise(0.005f)},
                                           {offset.x + Noise(0.002f), offset.y + Noise(0.002f), offset.z + Noise(0.002f)}, dt);
        CHECK(!f.Still());
        for (; t < 1.0f; t += dt) f.Sample({Noise(0.005f), 1.f + Noise(0.005f), Noise(0.005f)},
                                           {offset.x + Noise(0.002f), offset.y + Noise(0.002f), offset.z + Noise(0.002f)}, dt);
        CHECK(f.Still());
        const Vec3 b = f.GyroBias();
        CHECK(Near(b.x, offset.x, 0.002f) && Near(b.y, offset.y, 0.002f) && Near(b.z, offset.z, 0.002f));
        for (; t < 61.f; t += dt) f.Sample({Noise(0.005f), 1.f + Noise(0.005f), Noise(0.005f)},
                                           {offset.x + Noise(0.002f), offset.y + Noise(0.002f), offset.z + Noise(0.002f)}, dt);
        const Vec3 w = f.AngularVelocity();
        CHECK(w.x == 0.f && w.y == 0.f && w.z == 0.f);  // below the deadband once the offset is off
        std::printf("idle 60 s: orientation %.3f deg from reset (raw integration would be %.1f deg)\n",
                    AngleOf(f.Orientation()), Deg(Length(offset) * 61.f));
        CHECK(AngleOf(f.Orientation()) < 1.0f);
    }

    // Turning about Y (yaw, which gravity can't see) at 1 rad/s for 1 s is tracked.
    {
        Filter f;
        f.Sample({0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}, 0.f);
        for (float t = 0.f; t < 1.f - 1e-4f; t += dt) f.Sample({0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, dt);
        const Quat q = f.Orientation();
        std::printf("yaw 1 rad: %.2f deg about (%.3f %.3f %.3f)\n", AngleOf(q), q.x, q.y, q.z);
        CHECK(Near(AngleOf(q), Deg(1.f), 1.5f));
        CHECK(std::fabs(q.y) > 0.4f && Near(q.x, 0.f, 0.01f) && Near(q.z, 0.f, 0.01f));
    }

    // Pitching the front up 30 degrees (gyro and accelerometer agree): tracked.
    {
        Filter f;
        const float target = 30.f / 57.29578f, rate = 1.f;
        float a = 0.f;
        f.Sample({0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}, 0.f);
        while (a < target - 1e-4f) {
            const float step = std::fmin(rate * dt, target - a);
            a += step;
            const Vec3 g = Rotate(Conjugate(AxisAngle({1.f, 0.f, 0.f}, a)), up);
            f.Sample(g, {step / dt, 0.f, 0.f}, dt);
        }
        for (int i = 0; i < 60; ++i) f.Sample(Rotate(Conjugate(AxisAngle({1.f, 0.f, 0.f}, target)), up), {0.f, 0.f, 0.f}, dt);
        const Vec3 seen = Rotate(Conjugate(f.Orientation()), up);  // up as the estimate sees it
        const Vec3 real = Rotate(Conjugate(AxisAngle({1.f, 0.f, 0.f}, target)), up);
        CHECK(Near(seen.x, real.x, 0.02f) && Near(seen.y, real.y, 0.02f) && Near(seen.z, real.z, 0.02f));
    }

    // The gyro missed a 20 degree tilt (rate 0): the measured gravity pulls the estimate over.
    {
        Filter f;
        for (int i = 0; i < 30; ++i) f.Sample({0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}, dt);  // reference: level
        const Quat tilt = AxisAngle({0.f, 0.f, 1.f}, 20.f / 57.29578f);
        const Vec3 g = Rotate(Conjugate(tilt), up);
        for (float t = 0.f; t < 10.f; t += dt) f.Sample(g, {0.f, 0.f, 0.f}, dt);
        const Vec3 seen = Rotate(Conjugate(f.Orientation()), up);
        std::printf("tilt correction: estimate's up (%.3f %.3f %.3f), measured (%.3f %.3f %.3f)\n", seen.x, seen.y,
                    seen.z, g.x, g.y, g.z);
        CHECK(Near(seen.x, g.x, 0.02f) && Near(seen.y, g.y, 0.02f) && Near(seen.z, g.z, 0.02f));
    }

    // Shaken (2 G): no correction, no NaN.
    {
        Filter f;
        for (int i = 0; i < 30; ++i) f.Sample({0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}, dt);
        for (int i = 0; i < 30; ++i) f.Sample({1.2f, 1.6f, 0.f}, {0.5f, 0.2f, -0.3f}, dt);
        const Quat q = f.Orientation();
        CHECK(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w));
        CHECK(!f.Still());
        f.Reset();
        const Quat r = f.Orientation();
        CHECK(r.w == 1.f && r.x == 0.f);
    }

    // The virtual tilt: right side down to 35 degrees, then back to level.
    {
        Filter f;
        bool away = false;
        for (float t = 0.f; t < 0.5f; t += dt) away = f.VirtualTilt(1.f, 0.f, dt);
        CHECK(away);
        const Vec3 a = f.Acceleration();
        std::printf("tilt right: accel (%.3f %.3f %.3f)\n", a.x, a.y, a.z);
        CHECK(Near(a.x, -std::sin(kMaxTiltRad), 0.01f) && Near(a.y, std::cos(kMaxTiltRad), 0.01f) && Near(a.z, 0.f, 0.01f));
        CHECK(Near(AngleOf(f.Orientation()), Deg(kMaxTiltRad), 0.5f));
        for (float t = 0.f; t < 0.5f; t += dt) away = f.VirtualTilt(0.f, 0.f, dt);
        CHECK(!away);
        const Vec3 b = f.Acceleration();
        CHECK(b.x == 0.f && b.y == 1.f && b.z == 0.f);
        // Front up: the pad's +Z (towards the player) dips, so "up" leans to -Z.
        for (float t = 0.f; t < 0.5f; t += dt) f.VirtualTilt(0.f, 1.f, dt);
        const Vec3 c = f.Acceleration();
        CHECK(Near(c.z, -std::sin(kMaxTiltRad), 0.01f) && Near(c.x, 0.f, 0.01f));
        // Half a step eases, it doesn't jump.
        Filter g2;
        g2.VirtualTilt(1.f, 0.f, 0.05f);
        CHECK(Near(AngleOf(g2.Orientation()), Deg(kTiltRate * 0.05f), 0.2f));
    }

    // The mouse as the touch pad.
    {
        using namespace input::mouse_touch;
        Touch t;
        uint64_t now = 1'000'000'000;
        ps4::PadData d{};
        t.Fill(d, now);
        CHECK(d.touchData.touchNum == 0 && d.buttons == 0);
        t.Move(10.f, 5.f, now);
        d = {};
        t.Fill(d, now);
        CHECK(d.touchData.touchNum == 1);
        CHECK(d.touchData.touch[0].x == static_cast<uint16_t>(kWidth / 2 + 10) &&
              d.touchData.touch[0].y == static_cast<uint16_t>(kHeight / 2 + 5));
        const uint8_t firstId = d.touchData.touch[0].id;
        // Still for 0.3 s: lifted.
        d = {};
        t.Fill(d, now + kLiftAfterNs + 1);
        CHECK(d.touchData.touchNum == 0);
        // Down again at the centre with a new id.
        now += kLiftAfterNs + 10'000'000;
        t.Move(1.f, 0.f, now);
        d = {};
        t.Fill(d, now);
        CHECK(d.touchData.touchNum == 1 && d.touchData.touch[0].id != firstId);
        CHECK(d.touchData.touch[0].x == static_cast<uint16_t>(kWidth / 2 + 1));
        // Sliding into the right edge lifts the finger for a moment, then it goes down at the centre.
        int moves = 0;
        for (; moves < 100; ++moves) {
            t.Move(20.f, 0.f, now += 1'000'000);
            d = {};
            t.Fill(d, now);
            if (d.touchData.touchNum == 0) break;
        }
        CHECK(moves > 30 && moves < 60);  // about 900 units from the centre to the edge margin
        CHECK(d.touchData.touchNum == 0);
        t.Move(5.f, 0.f, now + 10'000'000);  // still inside the lift: ignored
        d = {};
        t.Fill(d, now + 10'000'000);
        CHECK(d.touchData.touchNum == 0);
        now += kLiftedForNs + 1'000'000;
        t.Move(5.f, 0.f, now);
        d = {};
        t.Fill(d, now);
        CHECK(d.touchData.touchNum == 1 && d.touchData.touch[0].x == static_cast<uint16_t>(kWidth / 2 + 5));
        // The left button presses the touch pad, with the finger on it.
        t.Button(true, now);
        d = {};
        t.Fill(d, now + kLiftAfterNs * 2);  // a held press doesn't lift
        CHECK((d.buttons & ps4::PAD_TOUCH_PAD) != 0 && d.touchData.touchNum == 1);
        t.Button(false, now + kLiftAfterNs * 2);
        d = {};
        t.Fill(d, now + kLiftAfterNs * 2 + 1'000'000);
        CHECK((d.buttons & ps4::PAD_TOUCH_PAD) == 0);
        // Wheel up: two fingers spreading from 400 units apart toward 520.
        now += 10'000'000'000ull;
        t.Wheel(1.f, false, now);
        d = {};
        t.Fill(d, now);
        CHECK(d.touchData.touchNum == 2);
        const float sep0 = std::fabs(float(d.touchData.touch[1].x) - float(d.touchData.touch[0].x));
        CHECK(Near(sep0, kGestureStart, 2.f));
        for (int i = 1; i <= 10; ++i) { d = {}; t.Fill(d, now + i * 20'000'000ull); }
        const float sep1 = std::fabs(float(d.touchData.touch[1].x) - float(d.touchData.touch[0].x));
        std::printf("spread: %.0f -> %.0f units\n", sep0, sep1);
        CHECK(Near(sep1, kGestureStart + kGestureStep, 2.f));
        CHECK(d.touchData.touch[0].id != d.touchData.touch[1].id);
        // The gesture ends 0.3 s after the last notch: both fingers lift.
        d = {};
        t.Fill(d, now + kGestureEndNs + 300'000'000);
        CHECK(d.touchData.touchNum == 0);
        // Ctrl + wheel: the two fingers turn 15 degrees about the centre.
        now += 5'000'000'000ull;
        t.Wheel(1.f, true, now);
        for (int i = 1; i <= 10; ++i) { d = {}; t.Fill(d, now + i * 20'000'000ull); }
        const float ang = std::atan2(float(d.touchData.touch[1].y) - float(d.touchData.touch[0].y),
                                     float(d.touchData.touch[1].x) - float(d.touchData.touch[0].x));
        std::printf("twist: %.1f deg\n", Deg(ang));
        CHECK(Near(Deg(ang), Deg(kTwistStep), 1.f));
        t.Release();
        d = {};
        t.Fill(d, now + 400'000'000ull);
        CHECK(d.touchData.touchNum == 0 && d.buttons == 0);
    }

    std::printf("motiontest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
