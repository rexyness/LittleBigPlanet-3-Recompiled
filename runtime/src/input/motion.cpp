// Motion filter for libScePad's pad data (see motion.h).
#include "input/motion.h"

#include <algorithm>
#include <cmath>

namespace input::motion {

namespace {

constexpr Quat kIdentity{0.f, 0.f, 0.f, 1.f};
constexpr Vec3 kUp{0.f, 1.f, 0.f};

Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Scale(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 Normalize(const Vec3& v) {
    const float l = Length(v);
    return l > 1e-6f ? Scale(v, 1.f / l) : kUp;
}
Quat NormalizeQ(const Quat& q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 1e-6f ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : kIdentity;
}
// Rotation by `angle` radians about the unit `axis`.
Quat AxisAngle(const Vec3& axis, float angle) {
    const float s = std::sin(angle * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}
// The virtual tilt's orientation: pitch about +X (front up), then roll the right side down
// (about -Z), in SDL's controller axes (+X right, +Y up, +Z towards the player).
Quat TiltQuat(float roll, float pitch) {
    return Multiply(AxisAngle({0.f, 0.f, 1.f}, -roll), AxisAngle({1.f, 0.f, 0.f}, pitch));
}

}  // namespace

float Length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Quat Multiply(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat Conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

Vec3 Rotate(const Quat& q, const Vec3& v) {
    const Quat p{v.x, v.y, v.z, 0.f};
    const Quat r = Multiply(Multiply(q, p), Conjugate(q));
    return {r.x, r.y, r.z};
}

void Filter::Reset() {
    m_q = kIdentity;
    m_refUp = kUp;
    m_haveRef = false;
    m_haveSample = false;
    m_accel = kUp;
    m_gyro = {0.f, 0.f, 0.f};
    m_bias = {0.f, 0.f, 0.f};
    m_stillSum = {0.f, 0.f, 0.f};
    m_stillFor = 0.f;
    m_biasKnown = false;
    m_tilt[0] = m_tilt[1] = 0.f;
}

void Filter::Sample(const Vec3& accelG, const Vec3& gyroRad, float dt) {
    dt = std::clamp(dt, 0.f, 0.25f);  // a pause (or the first sample) mustn't spin the estimate
    m_haveSample = true;
    m_accel = accelG;
    const float g = Length(accelG);
    const bool nearOneG = std::fabs(g - 1.f) < kStillAccel;

    if (!m_haveRef) {
        // The gravity at reset is the reference: the pad's pose then is the identity.
        m_refUp = (g > 0.5f && g < 1.5f) ? Normalize(accelG) : kUp;
        m_haveRef = true;
    }

    // Learn the gyro's offset while the pad lies still.
    if (Length(gyroRad) < kStillGyro && nearOneG) {
        m_stillFor += dt;
        m_stillSum = Add(m_stillSum, Scale(gyroRad, dt));
        if (m_stillFor >= kStillBeforeBias) {
            if (!m_biasKnown) {
                m_bias = Scale(m_stillSum, 1.f / m_stillFor);  // the average over the still stretch
                m_biasKnown = true;
            } else {
                const float k = std::min(1.f, dt / kBiasTimeConstant);
                m_bias = Add(m_bias, Scale(Sub(gyroRad, m_bias), k));
            }
        }
    } else {
        m_stillFor = 0.f;
        m_stillSum = {0.f, 0.f, 0.f};
    }

    Vec3 w = Sub(gyroRad, m_bias);
    if (Length(w) < kGyroDeadband) w = {0.f, 0.f, 0.f};
    m_gyro = w;

    // Lean toward the measured gravity (Mahony-style): the reference up seen from the pad as
    // the estimate has it, against the up the accelerometer measures; not while it's shaken.
    Vec3 turn = w;
    if (std::fabs(g - 1.f) < 0.15f) {
        const Vec3 estimated = Rotate(Conjugate(m_q), m_refUp);
        const Vec3 measured = Scale(accelG, 1.f / g);
        turn = Add(turn, Scale(Cross(measured, estimated), kTiltCorrection));
    }
    const float angle = Length(turn) * dt;
    if (angle > 0.f) m_q = NormalizeQ(Multiply(m_q, AxisAngle(Normalize(turn), angle)));
}

bool Filter::VirtualTilt(float roll, float pitch, float dt) {
    const float target[2] = {std::clamp(roll, -1.f, 1.f) * kMaxTiltRad, std::clamp(pitch, -1.f, 1.f) * kMaxTiltRad};
    const float step = kTiltRate * std::clamp(dt, 0.f, 0.25f);
    for (int i = 0; i < 2; ++i) {
        const float d = target[i] - m_tilt[i];
        m_tilt[i] += std::fabs(d) <= step ? d : (d > 0.f ? step : -step);
    }
    return m_tilt[0] != 0.f || m_tilt[1] != 0.f;
}

Quat Filter::Orientation() const {
    if (m_tilt[0] != 0.f || m_tilt[1] != 0.f) return TiltQuat(m_tilt[0], m_tilt[1]);
    return m_haveSample ? m_q : kIdentity;
}

Vec3 Filter::Acceleration() const {
    // The accelerometer reads "up" in the pad's frame: a tilted pad sees it tilted.
    if (m_tilt[0] != 0.f || m_tilt[1] != 0.f) return Rotate(Conjugate(TiltQuat(m_tilt[0], m_tilt[1])), kUp);
    return m_haveSample ? m_accel : kUp;
}

Vec3 Filter::AngularVelocity() const {
    if (m_tilt[0] != 0.f || m_tilt[1] != 0.f) return {0.f, 0.f, 0.f};
    return m_haveSample ? m_gyro : Vec3{0.f, 0.f, 0.f};
}

}  // namespace input::motion
