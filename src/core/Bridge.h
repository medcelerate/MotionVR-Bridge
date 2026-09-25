#pragma once

#include "core/Plugin.h"
#include "core/RateCounter.h"

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace mvr {

// Connects one source to one sink and tracks per-role activity for the UI.
class Bridge {
public:
    struct RoleState {
        bool live = false;      // received a pose recently
        float activity = 0.0f;  // 0..1, how much this point is moving
    };

    struct Snapshot {
        bool running = false;
        std::array<RoleState, kRoleCount> roles{};
        float frameRate = 0.0f;
        std::string sourceStatus;
        std::string sinkStatus;
    };

    Bridge();
    ~Bridge();

    bool start(std::unique_ptr<TrackingSource> source, const Config& sourceCfg,
               std::unique_ptr<TrackingSink> sink, const Config& sinkCfg, std::string& error);
    void stop();
    bool running() const;

    // Called with every frame from the source (before filtering), e.g. to
    // record it. Set before start().
    void setFrameTap(std::function<void(const TrackingFrame&)> tap) { tap_ = std::move(tap); }

    void runSourceAction(const std::string& key);
    void runSinkAction(const std::string& key);

    void setRoleEnabled(TrackerRole role, bool enabled);
    bool roleEnabled(TrackerRole role) const;

    Snapshot snapshot();

private:
    using Clock = std::chrono::steady_clock;

    void onFrame(const TrackingFrame& frame);

    std::unique_ptr<TrackingSource> source_;
    std::unique_ptr<TrackingSink> sink_;
    std::function<void(const TrackingFrame&)> tap_;

    mutable std::mutex mutex_;
    RoleSet enabled_;
    RoleSet supported_; // by the current sink
    TrackingFrame lastFrame_;
    std::array<Clock::time_point, kRoleCount> lastSeen_{};
    std::array<float, kRoleCount> activity_{};
    Clock::time_point lastFrameTime_{};
    RateCounter frameRate_;
};

} // namespace mvr
