#pragma once

#include "core/Tracking.h"

#include <openvr_driver.h>

#include <string>

namespace mvr {

// One virtual Vive-style tracker bound to a body role.
class TrackerDevice : public vr::ITrackedDeviceServerDriver {
public:
    explicit TrackerDevice(TrackerRole role);

    const std::string& serial() const { return serial_; }
    TrackerRole role() const { return role_; }

    // Publishes a new pose (or marks the tracker as lost if `pose` is null).
    void update(const vr::DriverPose_t* pose);

    vr::EVRInitError Activate(uint32_t objectId) override;
    void Deactivate() override;
    void EnterStandby() override {}
    void* GetComponent(const char*) override { return nullptr; }
    void DebugRequest(const char*, char* response, uint32_t size) override;
    vr::DriverPose_t GetPose() override { return pose_; }

private:
    TrackerRole role_;
    std::string serial_;
    uint32_t objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    vr::DriverPose_t pose_{};
};

// SteamVR controller type, which hints the tracker role ("vive_tracker_waist"
// etc.). Returns nullptr for roles that aren't published as trackers.
const char* viveTrackerType(TrackerRole role);

} // namespace mvr
