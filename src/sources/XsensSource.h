#pragma once

#include "core/Plugin.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace mvr {

// Receives Xsens MVN (Animate / Analyze) live data from its Network
// Streamer, including finger segments when gloves are used.
class XsensSource : public TrackingSource {
public:
    ~XsensSource() override;

    Config defaultConfig() const override;
    bool start(const Config& cfg, FrameHandler onFrame, std::string& error) override;
    void stop() override;
    std::string status() const override;

private:
    void run(std::string address, uint16_t port, int character, FrameHandler onFrame);
    void setStatus(std::string s);

    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::string status_;
};

} // namespace mvr
