#pragma once

#include "core/Tracking.h"

#include <deque>
#include <utility>

namespace mvr {

// Estimates the rigid transform (yaw + translation) from a capture system's
// world into SteamVR's tracking space by matching the capture system's head
// joint against the headset over time.
//
// Yaw is solved from the horizontal shape of the two head paths, so it needs
// the user to walk around a little; until then only translation is applied.
// This avoids depending on how the capture system orients its head joint.
class PlayspaceAlignment {
public:
    void reset();

    // Feed one pair of simultaneous head positions (source space, HMD space).
    void addSample(const Vec3& source, const Vec3& hmd);

    bool hasTranslation() const { return hasTranslation_; }
    bool hasYaw() const { return hasYaw_; }
    float yawRadians() const { return yaw_; }
    Quat rotation() const { return Quat::fromAxisAngle({0, 1, 0}, yaw_); }
    Vec3 translation() const { return translation_; }

    Vec3 apply(const Vec3& p) const;

    // Samples closer than this to the previous one are ignored.
    static constexpr float kMinSampleSpacing = 0.05f;
    // Horizontal RMS spread of the head path needed before yaw is trusted.
    static constexpr float kMinSpreadForYaw = 0.25f;
    static constexpr size_t kMinSamplesForYaw = 20;
    static constexpr size_t kMaxSamples = 400;

private:
    void solve();
    Vec3 rotate(const Vec3& p) const;

    std::deque<std::pair<Vec3, Vec3>> samples_;
    bool hasTranslation_ = false;
    bool hasYaw_ = false;
    float yaw_ = 0.0f;
    Vec3 translation_;
};

} // namespace mvr
