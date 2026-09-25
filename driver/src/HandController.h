#pragma once

#include "core/Tracking.h"

#include <openvr_driver.h>

#include <string>

namespace mvr {

// A hand published as a controller. It presents as a Valve Index
// ("knuckles") controller so games apply their existing Index bindings
// without any per-game setup.
class HandController : public vr::ITrackedDeviceServerDriver {
public:
    // `fullSkeleton`: fingers come from real finger tracking rather than
    // being estimated from buttons (reported to SteamVR at activation).
    HandController(Hand hand, bool fullSkeleton);

    const std::string& serial() const { return serial_; }

    // Publishes a pose, input state and hand skeleton, or disconnects the
    // controller if `pose` is null (so real controllers can take the hand
    // role back). Without finger data the skeleton follows trigger and grip.
    void update(const vr::DriverPose_t* pose, const ControllerInput& input, const FingerPose& fingers);

    vr::EVRInitError Activate(uint32_t objectId) override;
    void Deactivate() override;
    void EnterStandby() override {}
    void* GetComponent(const char*) override { return nullptr; }
    void DebugRequest(const char*, char* response, uint32_t size) override;
    vr::DriverPose_t GetPose() override { return pose_; }

private:
    struct Inputs {
        vr::VRInputComponentHandle_t systemClick, systemTouch;
        vr::VRInputComponentHandle_t aClick, aTouch, bClick, bTouch;
        vr::VRInputComponentHandle_t triggerClick, triggerTouch, triggerValue;
        vr::VRInputComponentHandle_t gripTouch, gripValue, gripForce;
        vr::VRInputComponentHandle_t stickClick, stickTouch, stickX, stickY;
        vr::VRInputComponentHandle_t fingerIndex, fingerMiddle, fingerRing, fingerPinky;
        vr::VRInputComponentHandle_t haptic;
        vr::VRInputComponentHandle_t skeleton;
    };

    void pushInput(const ControllerInput& in, const FingerPose& fingers);

    Hand hand_;
    bool fullSkeleton_;
    std::string serial_;
    uint32_t objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    vr::DriverPose_t pose_{};
    Inputs inputs_{};
};

} // namespace mvr
