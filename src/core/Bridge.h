#pragma once

#include "core/Plugin.h"
#include "core/RateCounter.h"

#include <array>
#include <chrono>
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
