#include "TrackerDevice.h"

#include <cstring>

namespace mvr {

const char* viveTrackerType(TrackerRole role)
{
    switch (role) {
    case TrackerRole::Hip: return "vive_tracker_waist";
    case TrackerRole::Chest: return "vive_tracker_chest";
    case TrackerRole::LeftElbow: return "vive_tracker_left_elbow";
    case TrackerRole::RightElbow: return "vive_tracker_right_elbow";
    case TrackerRole::LeftKnee: return "vive_tracker_left_knee";
    case TrackerRole::RightKnee: return "vive_tracker_right_knee";
    case TrackerRole::LeftFoot: return "vive_tracker_left_foot";
    case TrackerRole::RightFoot: return "vive_tracker_right_foot";
    default: return nullptr; // head is the HMD; hands belong to controllers
    }
}

TrackerDevice::TrackerDevice(TrackerRole role) : role_(role)
{
    // Stable serials so SteamVR remembers role assignments between sessions.
    serial_ = "MVR-";
    for (char c : roleName(role))
        if (c != ' ')
            serial_ += c;

    pose_.qWorldFromDriverRotation.w = 1;
    pose_.qDriverFromHeadRotation.w = 1;
    pose_.qRotation.w = 1;
    pose_.result = vr::TrackingResult_Uninitialized;
    pose_.deviceIsConnected = true;
}

vr::EVRInitError TrackerDevice::Activate(uint32_t objectId)
{
    objectId_ = objectId;
    auto* props = vr::VRProperties();
    const auto container = props->TrackedDeviceToPropertyContainer(objectId);

    props->SetStringProperty(container, vr::Prop_TrackingSystemName_String, "motionvrbridge");
    props->SetStringProperty(container, vr::Prop_ManufacturerName_String, "MotionVR Bridge");
    props->SetStringProperty(container, vr::Prop_ModelNumber_String, "MotionVR Bridge Tracker");
    props->SetStringProperty(container, vr::Prop_SerialNumber_String, serial_.c_str());
    props->SetStringProperty(container, vr::Prop_RenderModelName_String, "{htc}vr_tracker_vive_1_0");
    props->SetStringProperty(container, vr::Prop_ControllerType_String, viveTrackerType(role_));
    props->SetStringProperty(container, vr::Prop_InputProfilePath_String, "{htc}/input/vive_tracker_profile.json");
    props->SetInt32Property(container, vr::Prop_ControllerRoleHint_Int32, vr::TrackedControllerRole_OptOut);
    props->SetBoolProperty(container, vr::Prop_WillDriftInYaw_Bool, false);
    props->SetBoolProperty(container, vr::Prop_DeviceIsWireless_Bool, true);
    props->SetBoolProperty(container, vr::Prop_DeviceProvidesBatteryStatus_Bool, false);

    props->SetStringProperty(container, vr::Prop_NamedIconPathDeviceOff_String, "{htc}/icons/tracker_status_off.png");
    props->SetStringProperty(container, vr::Prop_NamedIconPathDeviceSearching_String, "{htc}/icons/tracker_status_searching.gif");
    props->SetStringProperty(container, vr::Prop_NamedIconPathDeviceSearchingAlert_String, "{htc}/icons/tracker_status_searching_alert.gif");
    props->SetStringProperty(container, vr::Prop_NamedIconPathDeviceReady_String, "{htc}/icons/tracker_status_ready.png");
    props->SetStringProperty(container, vr::Prop_NamedIconPathDeviceReadyAlert_String, "{htc}/icons/tracker_status_ready_alert.png");
    props->SetStringProperty(container, vr::Prop_NamedIconPathDeviceNotReady_String, "{htc}/icons/tracker_status_error.png");
    props->SetStringProperty(container, vr::Prop_NamedIconPathDeviceStandby_String, "{htc}/icons/tracker_status_standby.png");

    return vr::VRInitError_None;
}

void TrackerDevice::Deactivate()
{
    objectId_ = vr::k_unTrackedDeviceIndexInvalid;
}

void TrackerDevice::DebugRequest(const char*, char* response, uint32_t size)
{
    if (size > 0)
        response[0] = '\0';
}

void TrackerDevice::update(const vr::DriverPose_t* pose)
{
    if (pose) {
        pose_ = *pose;
    } else {
        pose_.poseIsValid = false;
        pose_.result = vr::TrackingResult_Running_OutOfRange;
    }
    if (objectId_ != vr::k_unTrackedDeviceIndexInvalid)
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(objectId_, pose_, sizeof(pose_));
}

} // namespace mvr
