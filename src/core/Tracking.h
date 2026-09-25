#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace mvr {

// Body points a source can provide and a sink can consume.
enum class TrackerRole : int {
    Head,
    Chest,
    Hip,
    LeftElbow,
    RightElbow,
    LeftHand,
    RightHand,
    LeftKnee,
    RightKnee,
    LeftFoot,
    RightFoot,
    Count
};

constexpr int kRoleCount = static_cast<int>(TrackerRole::Count);

constexpr std::string_view roleName(TrackerRole r)
{
    constexpr std::array<std::string_view, kRoleCount> names = {
        "Head", "Chest", "Hip", "Left Elbow", "Right Elbow", "Left Hand", "Right Hand",
        "Left Knee", "Right Knee", "Left Foot", "Right Foot",
    };
    return names[static_cast<int>(r)];
}

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const
    {
        float l = length();
        return l > 1e-9f ? *this * (1.0f / l) : Vec3{};
    }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    Quat operator*(const Quat& b) const
    {
        return {w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
    }

    Quat conjugate() const { return {-x, -y, -z, w}; }

    Vec3 rotate(const Vec3& v) const
    {
        const Vec3 u{x, y, z};
        const Vec3 t = u.cross(v) * 2.0f;
        return v + t * w + u.cross(t);
    }

    // Rotation whose local X/Y/Z axes map to the given orthonormal vectors.
    static Quat fromBasis(const Vec3& bx, const Vec3& by, const Vec3& bz)
    {
        const float trace = bx.x + by.y + bz.z;
        Quat q;
        if (trace > 0) {
            float s = std::sqrt(trace + 1.0f) * 2;
            q = {(by.z - bz.y) / s, (bz.x - bx.z) / s, (bx.y - by.x) / s, 0.25f * s};
        } else if (bx.x > by.y && bx.x > bz.z) {
            float s = std::sqrt(1.0f + bx.x - by.y - bz.z) * 2;
            q = {0.25f * s, (by.x + bx.y) / s, (bz.x + bx.z) / s, (by.z - bz.y) / s};
        } else if (by.y > bz.z) {
            float s = std::sqrt(1.0f + by.y - bx.x - bz.z) * 2;
            q = {(by.x + bx.y) / s, 0.25f * s, (bz.y + by.z) / s, (bz.x - bx.z) / s};
        } else {
            float s = std::sqrt(1.0f + bz.z - bx.x - by.y) * 2;
            q = {(bz.x + bx.z) / s, (bz.y + by.z) / s, 0.25f * s, (bx.y - by.x) / s};
        }
        return q;
    }

    static Quat fromAxisAngle(Vec3 axis, float radians)
    {
        float s = std::sin(radians * 0.5f);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
    }

    // Angle in radians between two orientations.
    static float angleBetween(const Quat& a, const Quat& b)
    {
        float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        return 2.0f * std::acos(std::fmin(d, 1.0f));
    }
};

constexpr float kDegToRad = 0.017453292519943295f;
constexpr float kRadToDeg = 57.29577951308232f;

struct TrackerPose {
    bool valid = false;
    Vec3 position;     // meters
    Quat orientation;
};

enum class Hand : int { Left, Right };

// Button/axis state for a hand controller, e.g. produced by gesture
// recognition and consumed by targets that publish controllers.
struct ControllerInput {
    enum Button : uint32_t {
        A = 1u << 0,
        B = 1u << 1,
        System = 1u << 2,
        TriggerClick = 1u << 3,
        GripClick = 1u << 4,
        ThumbstickClick = 1u << 5,
    };

    uint32_t buttons = 0;
    float trigger = 0.0f;     // 0..1
    float grip = 0.0f;        // 0..1
    float thumbstickX = 0.0f; // -1..1
    float thumbstickY = 0.0f; // -1..1

    bool pressed(Button b) const { return (buttons & b) != 0; }
};

// Canonical frame exchanged between sources and sinks:
// right-handed, +Y up, -Z forward, meters (the OpenXR / SteamVR convention).
struct TrackingFrame {
    uint64_t timestampUs = 0;
    std::array<TrackerPose, kRoleCount> poses{};
    std::array<ControllerInput, 2> controllers{}; // indexed by Hand

    TrackerPose& operator[](TrackerRole r) { return poses[static_cast<int>(r)]; }
    const TrackerPose& operator[](TrackerRole r) const { return poses[static_cast<int>(r)]; }
};

} // namespace mvr
