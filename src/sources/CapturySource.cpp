#include "sources/CapturySource.h"

#include "RemoteCaptury.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <initializer_list>

namespace mvr {

namespace {

bool nameEquals(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
            return false;
    return *a == *b;
}

int findJointByName(const CapturyActor& actor, std::initializer_list<const char*> names)
{
    for (const char* name : names)
        for (int j = 0; j < actor.numJoints; ++j)
            if (nameEquals(actor.joints[j].name, name))
                return j;
    return -1;
}

int findJointByBone(const CapturyActor& actor, CapturyBoneType type)
{
    for (int j = 0; j < actor.numJoints; ++j)
        if (actor.joints[j].boneType == type)
            return j;
    return -1;
}

// Captury rotations are XYZ Euler angles in degrees composed as Rz * Ry * Rx.
Quat eulerToQuat(const float* deg)
{
    const Quat qx = Quat::fromAxisAngle({1, 0, 0}, deg[0] * kDegToRad);
    const Quat qy = Quat::fromAxisAngle({0, 1, 0}, deg[1] * kDegToRad);
    const Quat qz = Quat::fromAxisAngle({0, 0, 1}, deg[2] * kDegToRad);
    return qz * qy * qx;
}

} // namespace

CapturySource::~CapturySource()
{
    stop();
}

Config CapturySource::defaultConfig() const
{
    return {
        {"host", "Captury Host", ConfigField::Kind::Text, "127.0.0.1", "Leave empty to auto-discover"},
        {"port", "Captury Port", ConfigField::Kind::Number, "2101", "Captury Live default is 2101"},
        {"actor", "Actor ID", ConfigField::Kind::Text, "", "Empty = first tracked actor"},
        {"scale", "Units to meters", ConfigField::Kind::Text, "0.001", "Captury streams millimeters"},
    };
}

bool CapturySource::start(const Config& cfg, FrameHandler onFrame, std::string& error)
{
    stop();

    const std::string host = configString(cfg, "host", "127.0.0.1");
    const int port = configInt(cfg, "port", 2101);
    if (port <= 0 || port > 65535) {
        error = "Invalid Captury port";
        return false;
    }
    const std::string actor = configString(cfg, "actor");
    requestedActor_ = actor.empty() ? -1 : configInt(cfg, "actor", -1);
    scale_ = configFloat(cfg, "scale", 0.001f);
    onFrame_ = std::move(onFrame);

    {
        std::lock_guard lock(mutex_);
        activeActor_ = -1;
        activeActorName_.clear();
        jointMaps_.clear();
        lastError_.clear();
        endpoint_ = host.empty() ? "auto-discovery" : host + ":" + std::to_string(port);
    }

    RemoteCaptury* rc = Captury_create();
    Captury_enablePrintf(rc, 0);
    Captury_registerLogCallback(rc, &CapturySource::logCallback, this);
    Captury_registerNewPoseCallback(rc, &CapturySource::poseCallback, this);
    Captury_registerActorChangedCallback(rc, &CapturySource::actorChangedCallback, this);

    // Async connect returns immediately; RemoteCaptury keeps reconnecting in
    // the background and (re)starts the stream requested below once connected.
    if (!Captury_connect2(rc, host.c_str(), static_cast<unsigned short>(port), 0, 0, 1, "", "")) {
        error = "Could not connect to Captury at " + endpoint_;
        Captury_destroy(rc);
        return false;
    }
    Captury_startStreaming(rc, CAPTURY_STREAM_POSES | CAPTURY_STREAM_COMPRESSED);

    std::lock_guard lock(mutex_);
    rc_ = rc;
    return true;
}

void CapturySource::stop()
{
    RemoteCaptury* rc = nullptr;
    {
        std::lock_guard lock(mutex_);
        std::swap(rc, rc_);
    }
    if (!rc)
        return;
    // Must not hold mutex_ here: destroying joins the receive thread, which
    // may be inside one of our callbacks.
    Captury_stopStreaming(rc);
    Captury_destroy(rc);
}

std::string CapturySource::status() const
{
    std::lock_guard lock(mutex_);
    if (!rc_)
        return "Stopped";

    switch (Captury_getConnectionStatus(rc_)) {
    case CAPTURY_CONNECTED:
        if (activeActor_ < 0)
            return "Connected to " + endpoint_ + " · waiting for a tracked actor";
        return "Connected to " + endpoint_ + " · actor " + std::to_string(activeActor_) +
               (activeActorName_.empty() ? "" : " (" + activeActorName_ + ")");
    case CAPTURY_CONNECTING:
        return "Connecting to " + endpoint_ + "…" + (lastError_.empty() ? "" : "\n" + lastError_);
    default:
        return "Disconnected from " + endpoint_ + (lastError_.empty() ? "" : "\n" + lastError_);
    }
}

void CapturySource::poseCallback(RemoteCaptury*, CapturyActor* actor, CapturyPose* pose, int, void* self)
{
    if (actor && pose)
        static_cast<CapturySource*>(self)->handlePose(*actor, *pose);
}

void CapturySource::actorChangedCallback(RemoteCaptury*, int actorId, int mode, void* selfPtr)
{
    auto* self = static_cast<CapturySource*>(selfPtr);
    std::lock_guard lock(self->mutex_);
    if (mode == ACTOR_DELETED)
        self->jointMaps_.erase(actorId); // ids can be reused with another skeleton
    if ((mode == ACTOR_DELETED || mode == ACTOR_STOPPED) && self->activeActor_ == actorId) {
        self->activeActor_ = -1;
        self->activeActorName_.clear();
    }
}

void CapturySource::logCallback(int level, const char* msg, void* selfPtr)
{
    // RemoteCaptury logs connection failures at INFO level, so keep anything
    // that looks like a problem.
    if (level > CAPTURY_LOG_WARNING && !std::strstr(msg, "cannot") && !std::strstr(msg, "failed"))
        return;
    auto* self = static_cast<CapturySource*>(selfPtr);
    std::string line(msg);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    std::lock_guard lock(self->mutex_);
    self->lastError_ = std::move(line);
}

void CapturySource::handlePose(const CapturyActor& actor, const CapturyPose& pose)
{
    JointMap map;
    {
        std::lock_guard lock(mutex_);
        if (activeActor_ < 0) {
            if (requestedActor_ >= 0 && actor.id != requestedActor_)
                return;
            activeActor_ = actor.id;
            activeActorName_ = actor.name;
            lastError_.clear();
        } else if (actor.id != activeActor_) {
            return;
        }

        auto it = jointMaps_.find(actor.id);
        if (it == jointMaps_.end())
            it = jointMaps_.emplace(actor.id, buildJointMap(actor)).first;
        map = it->second;
    }

    TrackingFrame frame;
    frame.timestampUs = pose.timestamp;
    for (int r = 0; r < kRoleCount; ++r) {
        const int j = map[r];
        if (j < 0 || j >= pose.numTransforms)
            continue;
        const CapturyTransform& t = pose.transforms[j];
        TrackerPose& out = frame.poses[r];
        out.valid = true;
        out.position = Vec3{t.translation[0], t.translation[1], t.translation[2]} * scale_;
        out.orientation = eulerToQuat(t.rotation);
    }
    onFrame_(frame);
}

CapturySource::JointMap CapturySource::buildJointMap(const CapturyActor& actor)
{
    JointMap map;
    map.fill(-1);
    auto set = [&](TrackerRole role, int joint) { map[static_cast<int>(role)] = joint; };

    // Prefer the functional bone types; older servers leave them unset, so
    // check they look sane before trusting them.
    int hipCount = 0;
    for (int j = 0; j < actor.numJoints; ++j)
        hipCount += actor.joints[j].boneType == CAPTURY_HIPS;
    const bool useBoneTypes = hipCount == 1 && findJointByBone(actor, CAPTURY_LEFT_KNEE) >= 0;

    if (useBoneTypes) {
        set(TrackerRole::Head, findJointByBone(actor, CAPTURY_HEAD));
        set(TrackerRole::Hip, findJointByBone(actor, CAPTURY_HIPS));
        set(TrackerRole::LeftElbow, findJointByBone(actor, CAPTURY_LEFT_ELBOW));
        set(TrackerRole::RightElbow, findJointByBone(actor, CAPTURY_RIGHT_ELBOW));
        set(TrackerRole::LeftHand, findJointByBone(actor, CAPTURY_LEFT_WRIST));
        set(TrackerRole::RightHand, findJointByBone(actor, CAPTURY_RIGHT_WRIST));
        set(TrackerRole::LeftKnee, findJointByBone(actor, CAPTURY_LEFT_KNEE));
        set(TrackerRole::RightKnee, findJointByBone(actor, CAPTURY_RIGHT_KNEE));
        set(TrackerRole::LeftFoot, findJointByBone(actor, CAPTURY_LEFT_ANKLE));
        set(TrackerRole::RightFoot, findJointByBone(actor, CAPTURY_RIGHT_ANKLE));

        // Chest: the upper-most spine joint, i.e. the neck's parent.
        int chest = findJointByBone(actor, CAPTURY_SPINE);
        int neck = findJointByBone(actor, CAPTURY_NECK);
        if (neck >= 0 && actor.joints[neck].parent >= 0 && actor.joints[neck].parent != map[int(TrackerRole::Hip)])
            chest = actor.joints[neck].parent;
        set(TrackerRole::Chest, chest);
    } else {
        set(TrackerRole::Head, findJointByName(actor, {"Head"}));
        set(TrackerRole::Chest, findJointByName(actor, {"Spine3", "Spine2", "Chest", "Spine1", "Spine"}));
        set(TrackerRole::Hip, findJointByName(actor, {"Hips", "Hip", "Pelvis", "Root"}));
        set(TrackerRole::LeftElbow, findJointByName(actor, {"LeftForeArm", "LeftElbow"}));
        set(TrackerRole::RightElbow, findJointByName(actor, {"RightForeArm", "RightElbow"}));
        set(TrackerRole::LeftHand, findJointByName(actor, {"LeftHand", "LeftWrist"}));
        set(TrackerRole::RightHand, findJointByName(actor, {"RightHand", "RightWrist"}));
        set(TrackerRole::LeftKnee, findJointByName(actor, {"LeftLeg", "LeftKnee"}));
        set(TrackerRole::RightKnee, findJointByName(actor, {"RightLeg", "RightKnee"}));
        set(TrackerRole::LeftFoot, findJointByName(actor, {"LeftFoot", "LeftAnkle"}));
        set(TrackerRole::RightFoot, findJointByName(actor, {"RightFoot", "RightAnkle"}));
    }
    return map;
}

} // namespace mvr
