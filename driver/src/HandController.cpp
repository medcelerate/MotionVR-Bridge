#include "HandController.h"

#include <cmath>

namespace mvr {

HandController::HandController(Hand hand) : hand_(hand)
{
    serial_ = hand == Hand::Left ? "MVR-LeftHand" : "MVR-RightHand";
    pose_.qWorldFromDriverRotation.w = 1;
    pose_.qDriverFromHeadRotation.w = 1;
    pose_.qRotation.w = 1;
    pose_.result = vr::TrackingResult_Uninitialized;
}

vr::EVRInitError HandController::Activate(uint32_t objectId)
{
    objectId_ = objectId;
    auto* props = vr::VRProperties();
    const auto c = props->TrackedDeviceToPropertyContainer(objectId);
    const bool left = hand_ == Hand::Left;

    props->SetStringProperty(c, vr::Prop_TrackingSystemName_String, "motionvrbridge");
    props->SetStringProperty(c, vr::Prop_ManufacturerName_String, "MotionVR Bridge");
    props->SetStringProperty(c, vr::Prop_ModelNumber_String, left ? "MotionVR Bridge Hand Left" : "MotionVR Bridge Hand Right");
    props->SetStringProperty(c, vr::Prop_SerialNumber_String, serial_.c_str());
    props->SetInt32Property(c, vr::Prop_ControllerRoleHint_Int32,
                            left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand);
    // Index controller identity so existing game bindings apply.
    props->SetStringProperty(c, vr::Prop_ControllerType_String, "knuckles");
    props->SetStringProperty(c, vr::Prop_InputProfilePath_String, "{indexcontroller}/input/index_controller_profile.json");
    props->SetStringProperty(c, vr::Prop_RenderModelName_String,
                             left ? "{indexcontroller}valve_controller_knu_1_0_left"
                                  : "{indexcontroller}valve_controller_knu_1_0_right");
    props->SetBoolProperty(c, vr::Prop_WillDriftInYaw_Bool, false);
    props->SetBoolProperty(c, vr::Prop_DeviceIsWireless_Bool, true);
    props->SetBoolProperty(c, vr::Prop_DeviceProvidesBatteryStatus_Bool, false);

    auto* input = vr::VRDriverInput();
    auto boolean = [&](const char* path, vr::VRInputComponentHandle_t& h) { input->CreateBooleanComponent(c, path, &h); };
    auto scalar = [&](const char* path, vr::VRInputComponentHandle_t& h, vr::EVRScalarUnits units) {
        input->CreateScalarComponent(c, path, &h, vr::VRScalarType_Absolute, units);
    };
    constexpr auto oneSided = vr::VRScalarUnits_NormalizedOneSided;
    constexpr auto twoSided = vr::VRScalarUnits_NormalizedTwoSided;

    boolean("/input/system/click", inputs_.systemClick);
    boolean("/input/system/touch", inputs_.systemTouch);
    boolean("/input/a/click", inputs_.aClick);
    boolean("/input/a/touch", inputs_.aTouch);
    boolean("/input/b/click", inputs_.bClick);
    boolean("/input/b/touch", inputs_.bTouch);
    boolean("/input/trigger/click", inputs_.triggerClick);
    boolean("/input/trigger/touch", inputs_.triggerTouch);
    scalar("/input/trigger/value", inputs_.triggerValue, oneSided);
    boolean("/input/grip/touch", inputs_.gripTouch);
    scalar("/input/grip/value", inputs_.gripValue, oneSided);
    scalar("/input/grip/force", inputs_.gripForce, oneSided);
    boolean("/input/thumbstick/click", inputs_.stickClick);
    boolean("/input/thumbstick/touch", inputs_.stickTouch);
    scalar("/input/thumbstick/x", inputs_.stickX, twoSided);
    scalar("/input/thumbstick/y", inputs_.stickY, twoSided);
    scalar("/input/finger/index", inputs_.fingerIndex, oneSided);
    scalar("/input/finger/middle", inputs_.fingerMiddle, oneSided);
    scalar("/input/finger/ring", inputs_.fingerRing, oneSided);
    scalar("/input/finger/pinky", inputs_.fingerPinky, oneSided);
    input->CreateHapticComponent(c, "/output/haptic", &inputs_.haptic);

    return vr::VRInitError_None;
}

void HandController::Deactivate()
{
    objectId_ = vr::k_unTrackedDeviceIndexInvalid;
}

void HandController::DebugRequest(const char*, char* response, uint32_t size)
{
    if (size > 0)
        response[0] = '\0';
}

void HandController::update(const vr::DriverPose_t* pose, const ControllerInput& input)
{
    if (pose) {
        pose_ = *pose;
    } else {
        pose_.poseIsValid = false;
        pose_.deviceIsConnected = false;
        pose_.result = vr::TrackingResult_Running_OutOfRange;
    }
    if (objectId_ == vr::k_unTrackedDeviceIndexInvalid)
        return;
    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(objectId_, pose_, sizeof(pose_));
    pushInput(pose ? input : ControllerInput{});
}

void HandController::pushInput(const ControllerInput& in)
{
    auto* input = vr::VRDriverInput();
    auto setBool = [&](vr::VRInputComponentHandle_t h, bool v) { input->UpdateBooleanComponent(h, v, 0); };
    auto setScalar = [&](vr::VRInputComponentHandle_t h, float v) { input->UpdateScalarComponent(h, v, 0); };
    using B = ControllerInput;

    const bool system = in.pressed(B::System);
    setBool(inputs_.systemClick, system);
    setBool(inputs_.systemTouch, system);
    setBool(inputs_.aClick, in.pressed(B::A));
    setBool(inputs_.aTouch, in.pressed(B::A));
    setBool(inputs_.bClick, in.pressed(B::B));
    setBool(inputs_.bTouch, in.pressed(B::B));

    const bool triggerClick = in.pressed(B::TriggerClick);
    const float trigger = triggerClick ? 1.0f : in.trigger;
    setBool(inputs_.triggerClick, triggerClick);
    setBool(inputs_.triggerTouch, trigger > 0.05f);
    setScalar(inputs_.triggerValue, trigger);

    const bool gripClick = in.pressed(B::GripClick);
    const float grip = gripClick ? 1.0f : in.grip;
    setBool(inputs_.gripTouch, grip > 0.05f);
    setScalar(inputs_.gripValue, grip);
    setScalar(inputs_.gripForce, gripClick ? 1.0f : 0.0f);

    const bool stickClick = in.pressed(B::ThumbstickClick);
    setBool(inputs_.stickClick, stickClick);
    setBool(inputs_.stickTouch, stickClick || std::hypot(in.thumbstickX, in.thumbstickY) > 0.1f);
    setScalar(inputs_.stickX, in.thumbstickX);
    setScalar(inputs_.stickY, in.thumbstickY);

    // Index reports per-finger curl; approximate it from trigger and grip so
    // the in-game hand closes naturally.
    setScalar(inputs_.fingerIndex, trigger);
    setScalar(inputs_.fingerMiddle, grip);
    setScalar(inputs_.fingerRing, grip);
    setScalar(inputs_.fingerPinky, grip);
}

} // namespace mvr
