#pragma once

#include "core/Plugin.h"

#include <memory>

namespace mvr {

// Streams subjects from a Vicon DataStream server (Shogun, Nexus, Tracker,
// Evoke). Works with full-body skeletons and with individually named rigid
// bodies (e.g. objects called "Waist", "LeftFoot").
class ViconSource : public TrackingSource {
public:
    ~ViconSource() override;

    Config defaultConfig() const override;
    bool start(const Config& cfg, FrameHandler onFrame, std::string& error) override;
    void stop() override;
    std::string status() const override;

private:
    struct Worker;
    std::shared_ptr<Worker> worker_;
};

} // namespace mvr
