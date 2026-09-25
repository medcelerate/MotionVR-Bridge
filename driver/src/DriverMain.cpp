// SteamVR driver entry point: receives MotionVR Bridge stream packets and publishes
// them as Vive-style trackers (and optionally hand controllers), aligned to
// the headset's playspace.

#include "HandController.h"
#include "PlayspaceAlignment.h"
#include "StreamReceiver.h"
#include "TrackerDevice.h"

#include "protocol/TrackerStream.h"

#include <openvr_driver.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#if defined(_WIN32)
#define MVR_DRIVER_EXPORT extern "C" __declspec(dllexport)
#else
#define MVR_DRIVER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace mvr {

namespace {

constexpr const char* kSettingsSection = "driver_motionvrbridge";
// Trackers go "out of range" if the app stops streaming for this long.
constexpr auto kStreamTimeout = std::chrono::milliseconds(500);

void log(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    vr::VRDriverLog()->Log(buf);
}

std::string settingString(const char* key, const char* fallback)
{
    char buf[256];
    vr::EVRSettingsError err = vr::VRSettingsError_None;
    vr::VRSettings()->GetString(kSettingsSection, key, buf, sizeof(buf), &err);
    return err == vr::VRSettingsError_None ? buf : fallback;
}

int settingInt(const char* key, int fallback)
{
    vr::EVRSettingsError err = vr::VRSettingsError_None;
    int v = vr::VRSettings()->GetInt32(kSettingsSection, key, &err);
    return err == vr::VRSettingsError_None ? v : fallback;
}

bool settingBool(const char* key, bool fallback)
{
    vr::EVRSettingsError err = vr::VRSettingsError_None;
    bool v = vr::VRSettings()->GetBool(kSettingsSection, key, &err);
    return err == vr::VRSettingsError_None ? v : fallback;
}

vr::HmdQuaternion_t toHmd(const Quat& q)
{
    return {q.w, q.x, q.y, q.z};
}

class DeviceProvider : public vr::IServerTrackedDeviceProvider {
public:
    vr::EVRInitError Init(vr::IVRDriverContext* context) override
    {
        VR_INIT_SERVER_DRIVER_CONTEXT(context);

        const std::string address = settingString("listen_address", "127.0.0.1");
        const int port = settingInt("port", stream::kDefaultPort);
        alignToHmd_ = settingBool("align_to_hmd", true);

        std::string error;
        if (!receiver_.start(address, static_cast<uint16_t>(port), error)) {
            log("MotionVR Bridge: %s", error.c_str());
            return vr::VRInitError_Init_Internal;
        }
        log("MotionVR Bridge: listening on %s:%d (align to HMD: %s)", address.c_str(), port, alignToHmd_ ? "on" : "off");
        return vr::VRInitError_None;
    }

    void Cleanup() override
    {
        receiver_.stop();
        // SteamVR has stopped calling into the devices by now.
        for (auto& d : devices_)
            d.reset();
        for (auto& c : controllers_)
            c.reset();
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }

    const char* const* GetInterfaceVersions() override { return vr::k_InterfaceVersions; }
    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}

    void RunFrame() override
    {
        vr::VREvent_t event;
        while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event))) {
        }

        const StreamReceiver::Latest latest = receiver_.latest();
        const bool fresh = latest.any && StreamReceiver::Clock::now() - latest.received < kStreamTimeout;

        if (latest.any && latest.session != session_) {
            session_ = latest.session;
            alignment_.reset();
            log("MotionVR Bridge: new stream session, realigning to HMD");
        }

        if (fresh && latest.sequence != lastSequence_) {
            lastSequence_ = latest.sequence;
            updateAlignment(latest.frame);
        }

        for (int r = 0; r < kRoleCount; ++r) {
            const auto role = static_cast<TrackerRole>(r);
            const TrackerPose& p = latest.frame.poses[r];
            if (!viveTrackerType(role))
                continue;

            if (fresh && p.valid) {
                vr::DriverPose_t pose = makePose(p);
                device(role).update(&pose);
            } else if (devices_[r]) {
                devices_[r]->update(nullptr);
            }
        }

        const bool handsWanted = fresh && (latest.flags & stream::kFlagHandControllers);
        for (Hand hand : {Hand::Left, Hand::Right}) {
            const int h = static_cast<int>(hand);
            const TrackerPose& p = latest.frame[hand == Hand::Left ? TrackerRole::LeftHand : TrackerRole::RightHand];
            if (handsWanted && p.valid) {
                vr::DriverPose_t pose = makePose(p);
                controller(hand).update(&pose, latest.frame.controllers[h]);
            } else if (controllers_[h]) {
                controllers_[h]->update(nullptr, {});
            }
        }
    }

private:
    TrackerDevice& device(TrackerRole role)
    {
        auto& slot = devices_[static_cast<int>(role)];
        if (!slot) {
            slot = std::make_unique<TrackerDevice>(role);
            vr::VRServerDriverHost()->TrackedDeviceAdded(slot->serial().c_str(), vr::TrackedDeviceClass_GenericTracker,
                                                         slot.get());
            log("MotionVR Bridge: added tracker %s", slot->serial().c_str());
        }
        return *slot;
    }

    // Created on first use: SteamVR can't remove devices, so controllers only
    // exist once the app has asked for them.
    HandController& controller(Hand hand)
    {
        auto& slot = controllers_[static_cast<int>(hand)];
        if (!slot) {
            slot = std::make_unique<HandController>(hand);
            vr::VRServerDriverHost()->TrackedDeviceAdded(slot->serial().c_str(), vr::TrackedDeviceClass_Controller,
                                                         slot.get());
            log("MotionVR Bridge: added controller %s", slot->serial().c_str());
        }
        return *slot;
    }

    void updateAlignment(const TrackingFrame& frame)
    {
        const TrackerPose& head = frame[TrackerRole::Head];
        if (!alignToHmd_ || !head.valid)
            return;

        vr::TrackedDevicePose_t hmd;
        vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, &hmd, 1);
        if (!hmd.bPoseIsValid)
            return;

        const auto& m = hmd.mDeviceToAbsoluteTracking.m;
        const bool hadYaw = alignment_.hasYaw();
        alignment_.addSample(head.position, {m[0][3], m[1][3], m[2][3]});
        if (!hadYaw && alignment_.hasYaw())
            log("MotionVR Bridge: playspace yaw locked at %.1f deg", alignment_.yawRadians() * kRadToDeg);
    }

    vr::DriverPose_t makePose(const TrackerPose& p) const
    {
        vr::DriverPose_t pose{};
        // Alignment goes in the world-from-driver transform, so poses stay in
        // the capture system's own space.
        pose.qWorldFromDriverRotation = toHmd(alignment_.rotation());
        const Vec3 t = alignment_.translation();
        pose.vecWorldFromDriverTranslation[0] = t.x;
        pose.vecWorldFromDriverTranslation[1] = t.y;
        pose.vecWorldFromDriverTranslation[2] = t.z;
        pose.qDriverFromHeadRotation.w = 1;

        pose.vecPosition[0] = p.position.x;
        pose.vecPosition[1] = p.position.y;
        pose.vecPosition[2] = p.position.z;
        pose.qRotation = toHmd(p.orientation);

        pose.result = vr::TrackingResult_Running_OK;
        pose.poseIsValid = true;
        pose.deviceIsConnected = true;
        return pose;
    }

    StreamReceiver receiver_;
    PlayspaceAlignment alignment_;
    bool alignToHmd_ = true;
    uint32_t session_ = 0;
    uint32_t lastSequence_ = 0;
    std::array<std::unique_ptr<TrackerDevice>, kRoleCount> devices_;
    std::array<std::unique_ptr<HandController>, 2> controllers_;
};

DeviceProvider g_provider;

} // namespace

} // namespace mvr

MVR_DRIVER_EXPORT void* HmdDriverFactory(const char* interfaceName, int* returnCode)
{
    if (std::strcmp(interfaceName, vr::IServerTrackedDeviceProvider_Version) == 0)
        return &mvr::g_provider;
    if (returnCode)
        *returnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
