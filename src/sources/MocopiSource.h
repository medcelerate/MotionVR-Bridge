#pragma once

#include "core/Plugin.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace mvr {

// Receives Sony mocopi motion from the mocopi app ("mocopi (UDP)"
// format), with the 27-joint mocopi skeleton.
class MocopiSource : public TrackingSource {
public:
    ~MocopiSource() override;

    Config defaultConfig() const override;
    bool start(const Config& cfg, FrameHandler onFrame, std::string& error) override;
    void stop() override;
    std::string status() const override;

private:
    void run(std::string address, uint16_t port, FrameHandler onFrame);
    void setStatus(std::string s);

    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::string status_;
};

} // namespace mvr
