#include "sinks/SteamVRSink.h"

#include "protocol/TrackerStream.h"

#include <cstdio>
#include <random>

namespace mvr {

Config SteamVRSink::defaultConfig() const
{
    return {
        {"host", "Driver Address", ConfigField::Kind::Text, "127.0.0.1", "Machine running SteamVR"},
        {"port", "Driver Port", ConfigField::Kind::Number, std::to_string(stream::kDefaultPort),
         "Must match driver_motionvrbridge's port setting"},
        {"hands_as_controllers", "Publish hands as controllers", ConfigField::Kind::Bool, "false",
         "Appear as Index controllers. Only use with no real controllers connected."},
        {"calibrate_hands", "Calibrate hands", ConfigField::Kind::Action, "",
         "Stand in a T-pose, palms down, then press"},
    };
}

RoleSet SteamVRSink::supportedRoles(const Config& cfg) const
{
    RoleSet roles;
    roles.set();
    if (!configBool(cfg, "hands_as_controllers", false)) {
        roles.reset(static_cast<int>(TrackerRole::LeftHand));
        roles.reset(static_cast<int>(TrackerRole::RightHand));
    }
    return roles;
}

bool SteamVRSink::start(const Config& cfg, std::string& error)
{
    const std::string host = configString(cfg, "host", "127.0.0.1");
    const int port = configInt(cfg, "port", stream::kDefaultPort);
    if (port <= 0 || port > 65535) {
        error = "Invalid driver port";
        return false;
    }
    if (!socket_.connectTo(host, static_cast<uint16_t>(port), error))
        return false;

    target_ = host + ":" + std::to_string(port);
    handsAsControllers_ = configBool(cfg, "hands_as_controllers", false);
    // A new session tells the driver to redo its playspace alignment.
    session_ = std::random_device{}();
    sequence_ = 0;
    hands_.reset();
    calibrateRequested_ = false;

    std::lock_guard lock(statsMutex_);
    packetRate_.reset();
    sendErrors_ = 0;
    handStatus_ = handsAsControllers_ ? "Hands: uncalibrated (using forearm direction)" : "";
    return true;
}

void SteamVRSink::stop()
{
    socket_.close();
}

void SteamVRSink::runAction(const std::string& key)
{
    if (key == "calibrate_hands")
        calibrateRequested_ = true;
}

void SteamVRSink::send(const TrackingFrame& frame, const RoleSet& forward)
{
    TrackingFrame out = frame;
    uint32_t flags = 0;
    if (handsAsControllers_) {
        flags |= stream::kFlagHandControllers;
        if (calibrateRequested_.exchange(false)) {
            const bool ok = hands_.calibrate(frame);
            std::lock_guard lock(statsMutex_);
            handStatus_ = ok ? "Hands: calibrated" : "Hands: calibration needs both elbows and wrists tracked";
        }
        out[TrackerRole::LeftHand] = hands_.controllerPose(frame, Hand::Left);
        out[TrackerRole::RightHand] = hands_.controllerPose(frame, Hand::Right);
    }
    for (int r = 0; r < kRoleCount; ++r)
        if (!forward.test(r))
            out.poses[r].valid = false;

    unsigned char buf[stream::kMaxPacketSize];
    const size_t size = stream::encode(out, session_, ++sequence_, flags, buf);
    const bool ok = socket_.send(buf, size);

    std::lock_guard lock(statsMutex_);
    ok ? packetRate_.add() : void(++sendErrors_);
}

std::string SteamVRSink::status() const
{
    std::lock_guard lock(statsMutex_);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Streaming to driver at %s · %.0f packets/s", target_.c_str(), packetRate_.rate());
    std::string s = buf;
    if (sendErrors_)
        s += " · " + std::to_string(sendErrors_) + " send errors";
    if (!handStatus_.empty())
        s += "\n" + handStatus_;
    return s;
}

} // namespace mvr
