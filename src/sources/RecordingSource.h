#pragma once

#include "core/Plugin.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace mvr {

// Plays a recorded take (.mvb) back into any target, optionally looping,
// at a different speed, and interpolated to a steady output rate.
class RecordingSource : public TrackingSource {
public:
    ~RecordingSource() override;

    Config defaultConfig() const override;
    bool start(const Config& cfg, FrameHandler onFrame, std::string& error) override;
    void stop() override;
    std::string status() const override;

private:
    void setStatus(std::string s);

    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::string status_;
};

} // namespace mvr
