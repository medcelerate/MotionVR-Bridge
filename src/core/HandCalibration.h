#pragma once

#include "core/Tracking.h"

#include <array>

namespace mvr {

// Turns a capture system's wrist into a controller pose in SteamVR's
// convention (-Z along the pointing direction, +Y out of the back of the
// hand, origin roughly at the palm).
//
// Before calibration the orientation is derived from the forearm direction,
// which assumes palms down and ignores wrist roll. Calibrating in a T-pose
// (palms down) learns the wrist joint's own axes, after which the wrist
// rotation is used directly, so roll follows the hand.
class HandCalibration {
public:
    // Returns true if both hands had the wrist and elbow data needed.
    bool calibrate(const TrackingFrame& frame);
    void reset();
    bool calibrated(Hand hand) const { return calibrated_[static_cast<int>(hand)]; }

    // Controller pose for `hand`, or an invalid pose if the wrist is missing.
    TrackerPose controllerPose(const TrackingFrame& frame, Hand hand) const;

    // Wrist-to-palm distance along the pointing direction.
    static constexpr float kPalmOffset = 0.07f;

private:
    std::array<Quat, 2> offset_{};
    std::array<bool, 2> calibrated_{};
};

// Controller orientation implied by the forearm, assuming palms down.
// Returns false if the arm is too short or points straight up or down.
bool forearmOrientation(const Vec3& elbow, const Vec3& wrist, Quat& out);

} // namespace mvr
