#include "core/HandCalibration.h"

namespace mvr {

namespace {

struct ArmRoles {
    TrackerRole elbow, wrist;
};

ArmRoles armRoles(Hand hand)
{
    return hand == Hand::Left ? ArmRoles{TrackerRole::LeftElbow, TrackerRole::LeftHand}
                              : ArmRoles{TrackerRole::RightElbow, TrackerRole::RightHand};
}

} // namespace

bool forearmOrientation(const Vec3& elbow, const Vec3& wrist, Quat& out)
{
    const Vec3 forearm = wrist - elbow;
    if (forearm.length() < 0.05f)
        return false;
    const Vec3 pointing = forearm.normalized();
    const Vec3 worldUp{0, 1, 0};
    const Vec3 up = (worldUp - pointing * pointing.dot(worldUp)).normalized();
    if (up.length() < 0.5f)
        return false; // arm is vertical; "palms down" is undefined
    const Vec3 z = pointing * -1.0f;
    out = Quat::fromBasis(up.cross(z), up, z);
    return true;
}

bool HandCalibration::calibrate(const TrackingFrame& frame)
{
    bool ok = true;
    for (Hand hand : {Hand::Left, Hand::Right}) {
        const auto [elbowRole, wristRole] = armRoles(hand);
        const TrackerPose& elbow = frame[elbowRole];
        const TrackerPose& wrist = frame[wristRole];
        Quat target;
        if (!elbow.valid || !wrist.valid || !forearmOrientation(elbow.position, wrist.position, target)) {
            ok = false;
            continue;
        }
        // wrist * offset == target at calibration time.
        const int h = static_cast<int>(hand);
        offset_[h] = wrist.orientation.conjugate() * target;
        calibrated_[h] = true;
    }
    return ok;
}

void HandCalibration::reset()
{
    offset_ = {};
    calibrated_ = {};
}

TrackerPose HandCalibration::controllerPose(const TrackingFrame& frame, Hand hand) const
{
    const auto [elbowRole, wristRole] = armRoles(hand);
    const TrackerPose& wrist = frame[wristRole];
    if (!wrist.valid)
        return {};

    TrackerPose out;
    const int h = static_cast<int>(hand);
    if (calibrated_[h]) {
        out.orientation = wrist.orientation * offset_[h];
    } else {
        const TrackerPose& elbow = frame[elbowRole];
        if (!elbow.valid || !forearmOrientation(elbow.position, wrist.position, out.orientation))
            return {};
    }
    out.valid = true;
    out.position = wrist.position + out.orientation.rotate({0, 0, -kPalmOffset});
    return out;
}

} // namespace mvr
