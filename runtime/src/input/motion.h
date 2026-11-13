// Motion data for libScePad's PadData (orientation, acceleration, angularVelocity), per pad.
//
// A PS4 pad reports the accelerometer in G (about +1 on Y while it lies flat, SDL's axes:
// +X right, +Y up, +Z towards the player), the gyro in rad/s, and an orientation quaternion
// that starts at identity when the pad is opened or scePadResetOrientation is called. LBP3
// leans Sackboy's head from the acceleration (research/lbppc_platform.md, input), so that is
// passed through raw; the gyro and the orientation are cleaned up here:
//  - gyro offset: a resting gyro reads a small rate that would turn the orientation forever.
//    While the pad is still (barely turning, about 1 G) for half a second, its average rate is
//    learned as the offset and taken off; tiny leftover rates are reported as 0.
//  - orientation: the corrected gyro integrated from identity at reset, leaned slowly toward the
//    gravity the accelerometer measures relative to the gravity it measured at reset, so tilt
//    doesn't drift (yaw, which gravity can't see, only drifts by what the offset misses).
//  - virtual tilt: keys (or anything) tilt a pad without sensors; while it is away from level it
//    replaces the sensors' tilt: the acceleration shows gravity tilted, the orientation the tilt.
// Written for this port (the idea of learning the offset while still, and of a virtual tilt for
// pads without sensors, as described in LBPPC's docs/input-research.md; no code taken from it).
// LBP3_MOTION=raw restores the old pass-through (identity orientation, raw gyro, -1 G at rest).
#pragma once

namespace input::motion {

struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };  // PadData's order (x, y, z, w)

// Tunables (exposed for the tests).
constexpr float kStillGyro = 0.06f;      // rad/s: turning slower than this...
constexpr float kStillAccel = 0.10f;     // ...and within this many G of 1 G counts as still
constexpr float kStillBeforeBias = 0.5f;  // s still before the offset is learned
constexpr float kBiasTimeConstant = 1.0f; // s: the learned offset follows the still rate this fast
constexpr float kGyroDeadband = 0.008f;   // rad/s: smaller corrected rates are reported as 0
constexpr float kTiltCorrection = 1.0f;   // 1/s: how fast the orientation leans to measured gravity
constexpr float kMaxTiltRad = 0.61f;      // a full virtual tilt (35 degrees)
constexpr float kTiltRate = 4.0f;         // rad/s the virtual tilt eases toward its target

class Filter {
public:
    Filter() { Reset(); }
    // Orientation back to identity; the next sensor sample becomes the reference gravity.
    void Reset();
    // A sensor sample: accel in G, gyro in rad/s, dt in seconds since the previous sample
    // (clamped; 0 on the first one after a reset).
    void Sample(const Vec3& accelG, const Vec3& gyroRad, float dt);
    // Virtual tilt target: roll (+ = right side down) and pitch (+ = front up), each -1..1 of a
    // full tilt, eased over dt. Returns true while the virtual tilt is away from level.
    bool VirtualTilt(float roll, float pitch, float dt);
    // The values for PadData. Without sensor samples (and no virtual tilt): identity, +1 G on Y,
    // no rotation.
    Quat Orientation() const;
    Vec3 Acceleration() const;
    Vec3 AngularVelocity() const;

    // For tests and the log.
    Vec3 GyroBias() const { return m_bias; }
    bool Still() const { return m_stillFor >= kStillBeforeBias; }
    bool HasSensors() const { return m_haveSample; }

private:
    Quat m_q;          // sensor orientation (identity at reset)
    Vec3 m_refUp;      // gravity direction (unit, pad frame) measured at reset
    bool m_haveRef;
    bool m_haveSample;
    Vec3 m_accel;      // last raw accel (G)
    Vec3 m_gyro;       // last corrected rate (rad/s)
    Vec3 m_bias;       // learned gyro offset (rad/s)
    Vec3 m_stillSum;   // rates summed over the current still stretch (for the first estimate)
    float m_stillFor;  // seconds the pad has been still
    bool m_biasKnown;
    float m_tilt[2];   // virtual roll, pitch (radians)
};

// Math helpers (exposed for the tests).
Quat Multiply(const Quat& a, const Quat& b);
Vec3 Rotate(const Quat& q, const Vec3& v);  // v rotated by q
Quat Conjugate(const Quat& q);
float Length(const Vec3& v);

}  // namespace input::motion
