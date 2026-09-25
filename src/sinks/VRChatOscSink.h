#pragma once

#include "core/Plugin.h"
#include "core/RateCounter.h"
#include "net/OscSender.h"

#include <mutex>

namespace mvr {

// Sends body trackers to VRChat's OSC tracker input.
// https://docs.vrchat.com/docs/osc-trackers
class VRChatOscSink : public TrackingSink {
public:
    Config defaultConfig() const override;
    RoleSet supportedRoles(const Config& cfg) const override;
    bool start(const Config& cfg, std::string& error) override;
    void stop() override;
    void send(const TrackingFrame& frame, const RoleSet& forward) override;
    std::string status() const override;

private:
    OscSender osc_;
    std::string target_;
    bool sendHead_ = true;

    mutable std::mutex statsMutex_;
    RateCounter messageRate_;
    int sendErrors_ = 0;
};

} // namespace mvr
