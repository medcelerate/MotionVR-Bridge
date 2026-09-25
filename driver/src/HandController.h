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
    explicit HandController(Hand hand);

    const std::string& serial() const { return serial_; }

    // Publishes a pose and input state, or disconnects the controller if
    // `pose` is null (so real controllers can take the hand role back).
    void update(const vr::DriverPose_t* pose, const ControllerInput& input);

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
    };

    void pushInput(const ControllerInput& in);

    Hand hand_;
    std::string serial_;
    uint32_t objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    vr::DriverPose_t pose_{};
    Inputs inputs_{};
};

} // namespace mvr
