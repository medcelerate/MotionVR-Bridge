#pragma once

#include "core/Tracking.h"

#include <bitset>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mvr {

// A user-editable setting shown in the UI next to a source or target.
struct ConfigField {
    // Action is a button, only enabled while running; it triggers runAction(key).
    // Choice is a dropdown whose value is one of `options`.
    enum class Kind { Text, Number, Bool, Action, Choice };

    std::string key;
    std::string label;
    Kind kind = Kind::Text;
    std::string value;
    std::string hint;
    std::vector<std::string> options;
};

using Config = std::vector<ConfigField>;

std::string configString(const Config& cfg, const std::string& key, const std::string& fallback = {});
int configInt(const Config& cfg, const std::string& key, int fallback = 0);
float configFloat(const Config& cfg, const std::string& key, float fallback = 0.0f);
bool configBool(const Config& cfg, const std::string& key, bool fallback = false);

using RoleSet = std::bitset<kRoleCount>;

// Produces TrackingFrames. start() must not block; frames are delivered on a
// source-owned thread until stop() returns.
class TrackingSource {
public:
    using FrameHandler = std::function<void(const TrackingFrame&)>;

    virtual ~TrackingSource() = default;
    virtual Config defaultConfig() const = 0;
    virtual bool start(const Config& cfg, FrameHandler onFrame, std::string& error) = 0;
    virtual void stop() = 0;
    virtual std::string status() const = 0;
    // Called from the UI thread for Action fields while running.
    virtual void runAction(const std::string& /*key*/) {}
};

// Consumes TrackingFrames. send() is called from the source's thread with the
// full frame; only roles in `forward` should be published (the rest may still
// be used to derive other data, e.g. the forearm for a hand pose).
class TrackingSink {
public:
    virtual ~TrackingSink() = default;
    virtual Config defaultConfig() const = 0;
    virtual RoleSet supportedRoles(const Config& cfg) const = 0;
    virtual bool start(const Config& cfg, std::string& error) = 0;
    virtual void stop() = 0;
    virtual void send(const TrackingFrame& frame, const RoleSet& forward) = 0;
    virtual std::string status() const = 0;
    // Called from the UI thread for Action fields while running.
    virtual void runAction(const std::string& /*key*/) {}
};

struct SourceInfo {
    std::string name;
    std::function<std::unique_ptr<TrackingSource>()> create;
};

struct SinkInfo {
    std::string name;
    std::function<std::unique_ptr<TrackingSink>()> create;
};

const std::vector<SourceInfo>& availableSources();
const std::vector<SinkInfo>& availableSinks();

} // namespace mvr
