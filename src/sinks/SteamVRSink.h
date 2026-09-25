#pragma once

#include "core/HandCalibration.h"
#include "core/Plugin.h"
#include "core/RateCounter.h"
#include "net/UdpSocket.h"

#include <atomic>
#include <mutex>

namespace mvr {

// Streams frames to the MotionVR Bridge SteamVR driver (driver/), which publishes
// them as Vive-style trackers and, optionally, the hands as controllers.
class SteamVRSink : public TrackingSink {
public:
    Config defaultConfig() const override;
    RoleSet supportedRoles(const Config& cfg) const override;
    bool start(const Config& cfg, std::string& error) override;
    void stop() override;
    void send(const TrackingFrame& frame, const RoleSet& forward) override;
    std::string status() const override;
    void runAction(const std::string& key) override;

private:
    UdpSocket socket_;
    std::string target_;
    uint32_t session_ = 0;
    uint32_t sequence_ = 0;
    bool handsAsControllers_ = false;

    HandCalibration hands_; // only touched on the send() thread
    std::atomic<bool> calibrateRequested_{false};

    mutable std::mutex statsMutex_;
    RateCounter packetRate_;
    int sendErrors_ = 0;
    std::string handStatus_;
};

} // namespace mvr
