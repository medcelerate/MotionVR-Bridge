#include "sinks/VRChatOscSink.h"

#include <cstdio>

namespace mvr {

namespace {

// VRChat accepts eight numbered body trackers and assigns them to body parts
// during calibration, so the slot numbers only need to be stable.
struct Slot {
    TrackerRole role;
    const char* id;
};

constexpr Slot kSlots[] = {
    {TrackerRole::Hip, "1"},
    {TrackerRole::Chest, "2"},
    {TrackerRole::LeftFoot, "3"},
    {TrackerRole::RightFoot, "4"},
    {TrackerRole::LeftKnee, "5"},
    {TrackerRole::RightKnee, "6"},
    {TrackerRole::LeftElbow, "7"},
    {TrackerRole::RightElbow, "8"},
    {TrackerRole::Head, "head"},
};

struct UnityPose {
    Vec3 position;
    Vec3 eulerDeg;
};

// Canonical right-handed frame -> Unity's left-handed frame by mirroring Z,
// then quaternion -> Unity Euler angles (applied Z, then X, then Y, i.e.
// R = Ry * Rx * Rz), which is what VRChat expects.
UnityPose toUnity(const TrackerPose& p)
{
    UnityPose out;
    out.position = {p.position.x, p.position.y, -p.position.z};

    const Quat q{-p.orientation.x, -p.orientation.y, p.orientation.z, p.orientation.w};
    const float m02 = 2 * (q.x * q.z + q.w * q.y);
    const float m10 = 2 * (q.x * q.y + q.w * q.z);
    const float m11 = 1 - 2 * (q.x * q.x + q.z * q.z);
    const float m12 = 2 * (q.y * q.z - q.w * q.x);
    const float m22 = 1 - 2 * (q.x * q.x + q.y * q.y);

    const float sx = std::fmax(-1.0f, std::fmin(1.0f, -m12));
    out.eulerDeg.x = std::asin(sx) * kRadToDeg;
    if (std::fabs(sx) < 0.9999f) {
        out.eulerDeg.y = std::atan2(m02, m22) * kRadToDeg;
        out.eulerDeg.z = std::atan2(m10, m11) * kRadToDeg;
    } else {
        // Gimbal lock: fold Z into Y.
        const float m00 = 1 - 2 * (q.y * q.y + q.z * q.z);
        const float m20 = 2 * (q.x * q.z - q.w * q.y);
        out.eulerDeg.y = std::atan2(-m20, m00) * kRadToDeg;
        out.eulerDeg.z = 0.0f;
    }
    return out;
}

} // namespace

Config VRChatOscSink::defaultConfig() const
{
    return {
        {"host", "OSC Address", ConfigField::Kind::Text, "127.0.0.1", "Machine running VRChat"},
        {"port", "OSC Port", ConfigField::Kind::Number, "9000", "VRChat listens on 9000"},
        {"send_head", "Send head for alignment", ConfigField::Kind::Bool, "true",
         "Lets VRChat align tracker space to your headset"},
    };
}

RoleSet VRChatOscSink::supportedRoles(const Config&) const
{
    RoleSet roles;
    for (const Slot& s : kSlots)
        roles.set(static_cast<int>(s.role));
    return roles;
}

bool VRChatOscSink::start(const Config& cfg, std::string& error)
{
    const std::string host = configString(cfg, "host", "127.0.0.1");
    const int port = configInt(cfg, "port", 9000);
    if (port <= 0 || port > 65535) {
        error = "Invalid OSC port";
        return false;
    }
    if (!osc_.open(host, static_cast<uint16_t>(port), error))
        return false;

    sendHead_ = configBool(cfg, "send_head", true);
    target_ = host + ":" + std::to_string(port);

    std::lock_guard lock(statsMutex_);
    messageRate_.reset();
    sendErrors_ = 0;
    return true;
}

void VRChatOscSink::stop()
{
    osc_.close();
}

void VRChatOscSink::send(const TrackingFrame& frame, const RoleSet& forward)
{
    int sent = 0, failed = 0;
    std::string base;
    for (const Slot& s : kSlots) {
        const TrackerPose& pose = frame[s.role];
        if (!pose.valid || !forward.test(static_cast<int>(s.role)) || (s.role == TrackerRole::Head && !sendHead_))
            continue;

        const UnityPose u = toUnity(pose);
        base = std::string("/tracking/trackers/") + s.id;
        bool ok = osc_.sendVec3(base + "/position", u.position.x, u.position.y, u.position.z) &&
                  osc_.sendVec3(base + "/rotation", u.eulerDeg.x, u.eulerDeg.y, u.eulerDeg.z);
        ok ? ++sent : ++failed;
    }

    std::lock_guard lock(statsMutex_);
    messageRate_.add(sent * 2);
    sendErrors_ += failed;
}

std::string VRChatOscSink::status() const
{
    std::lock_guard lock(statsMutex_);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Sending to %s · %.0f msg/s", target_.c_str(), messageRate_.rate());
    std::string s = buf;
    if (sendErrors_)
        s += " · " + std::to_string(sendErrors_) + " send errors";
    return s;
}

} // namespace mvr
