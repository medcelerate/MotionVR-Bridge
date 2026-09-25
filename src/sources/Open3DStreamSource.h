#pragma once

#include "core/Plugin.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace mvr {

// Receives skeletons and rigid bodies from any Open3DStream sender
// (MotionBuilder, Maya, Unreal, ...) over NNG or UDP.
// https://www.open3dstream.com/
class Open3DStreamSource : public TrackingSource {
public:
    ~Open3DStreamSource() override;

    Config defaultConfig() const override;
    bool start(const Config& cfg, FrameHandler onFrame, std::string& error) override;
    void stop() override;
    std::string status() const override;

private:
    void runNng(Config cfg, FrameHandler onFrame);
    void runUdp(Config cfg, FrameHandler onFrame);
    void setStatus(std::string s);

    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::string status_;
};

} // namespace mvr
