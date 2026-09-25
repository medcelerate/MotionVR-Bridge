#pragma once

#include "core/Plugin.h"

#include <atomic>
#include <thread>

namespace mvr {

// Synthetic "marching in place and waving" body for testing targets without
// a capture system.
class TestPatternSource : public TrackingSource {
public:
    ~TestPatternSource() override;

    Config defaultConfig() const override;
    bool start(const Config& cfg, FrameHandler onFrame, std::string& error) override;
    void stop() override;
    std::string status() const override;

private:
    void run(FrameHandler onFrame, int rateHz, float speed, float height);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> rateHz_{0};
};

} // namespace mvr
