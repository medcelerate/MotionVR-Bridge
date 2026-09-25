#pragma once

#include "core/Plugin.h"

#include <array>
#include <mutex>
#include <unordered_map>
#include <vector>

struct RemoteCaptury;
struct CapturyActor;
struct CapturyPose;

namespace mvr {

// Streams skeleton poses from Captury Live via RemoteCaptury.
class CapturySource : public TrackingSource {
public:
    ~CapturySource() override;

    Config defaultConfig() const override;
    bool start(const Config& cfg, FrameHandler onFrame, std::string& error) override;
    void stop() override;
    std::string status() const override;

private:
    struct JointMap {
        std::array<int, kRoleCount> roles; // role -> joint index, -1 if absent
        std::array<std::array<std::vector<int>, kFingerCount>, 2> fingers; // per hand, knuckle first
    };

    static void poseCallback(RemoteCaptury*, CapturyActor* actor, CapturyPose* pose, int quality, void* self);
    static void actorChangedCallback(RemoteCaptury*, int actorId, int mode, void* self);
    static void logCallback(int level, const char* msg, void* self);

    void handlePose(const CapturyActor& actor, const CapturyPose& pose);
    static JointMap buildJointMap(const CapturyActor& actor);

    RemoteCaptury* rc_ = nullptr;
    FrameHandler onFrame_;
    float scale_ = 0.001f;
    int requestedActor_ = -1; // -1: follow the first actor that streams

    mutable std::mutex mutex_;
    int activeActor_ = -1;
    std::string activeActorName_;
    std::unordered_map<int, JointMap> jointMaps_;
    std::string lastError_;
    std::string endpoint_;
};

} // namespace mvr
