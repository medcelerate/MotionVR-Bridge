#include "mocopi/MocopiDecoder.h"

#include <cstring>

namespace mvr::mocopi {

namespace {

// Sony's 27-joint skeleton (mocopi developer site, Technical Specification):
// 0 root, 1-7 torso_1..7, 8 neck_1, 9 neck_2, 10 head,
// 11-14 l_shoulder, l_up_arm, l_low_arm, l_hand, 15-18 the same on the right,
// 19-22 l_up_leg, l_low_leg, l_foot, l_toes, 23-26 the same on the right.
// Parents, used until the stream's own skeleton definition arrives.
constexpr int kDefaultParents[MocopiDecoder::kJointCount] = {
    -1, 0, 1, 2, 3, 4, 5, 6,  // root, torso_1..7
    7, 8, 9,                  // neck_1, neck_2, head
    7, 11, 12, 13,            // left arm
    7, 15, 16, 17,            // right arm
    0, 19, 20, 21,            // left leg
    0, 23, 24, 25,            // right leg
};

struct RoleJoint {
    TrackerRole role;
    int joint;
};

constexpr RoleJoint kRoleJoints[] = {
    {TrackerRole::Hip, 0},       {TrackerRole::Chest, 7},      {TrackerRole::Head, 10},
    {TrackerRole::LeftElbow, 13}, {TrackerRole::LeftHand, 14}, {TrackerRole::RightElbow, 17},
    {TrackerRole::RightHand, 18}, {TrackerRole::LeftKnee, 20}, {TrackerRole::LeftFoot, 21},
    {TrackerRole::RightKnee, 24}, {TrackerRole::RightFoot, 25},
};

uint32_t readU32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// Little-endian integer of 1, 2 or 4 bytes.
bool readInt(const uint8_t* p, size_t len, int& out)
{
    if (len == 1)
        out = p[0];
    else if (len == 2)
        out = static_cast<int16_t>(p[0] | p[1] << 8);
    else if (len == 4)
        out = static_cast<int32_t>(readU32(p));
    else
        return false;
    return true;
}

float readF32(const uint8_t* p)
{
    const uint32_t bits = readU32(p);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

bool isContainer(const char* name)
{
    for (const char* c : {"head", "sndf", "skdf", "bons", "fram", "btrs"})
        if (std::memcmp(name, c, 4) == 0)
            return true;
    return false;
}

} // namespace

MocopiDecoder::MocopiDecoder()
{
    for (int i = 0; i < kJointCount; ++i)
        joints_[i].parent = kDefaultParents[i];
}

bool MocopiDecoder::add(const uint8_t* data, size_t size, TrackingFrame& frame, std::string& error)
{
    sawFrame_ = false;
    if (!walk(data, size, 0)) {
        error = "Not a mocopi packet (set the mocopi app's format to \"mocopi (UDP)\")";
        return false;
    }
    if (!sawFrame_)
        return false; // skeleton definition or other metadata
    ++frames_;
    frame = toFrame();
    return true;
}

bool MocopiDecoder::walk(const uint8_t* data, size_t size, int depth)
{
    if (depth > 8)
        return false;
    size_t pos = 0;
    while (pos < size) {
        if (size - pos < 8)
            return false;
        const uint32_t len = readU32(data + pos);
        const char* name = reinterpret_cast<const char*>(data + pos + 4);
        pos += 8;
        if (len > size - pos)
            return false;
        const uint8_t* value = data + pos;
        pos += len;

        if (std::memcmp(name, "bndt", 4) == 0 || std::memcmp(name, "btdt", 4) == 0) {
            if (!readBone(value, len, name[1] == 'n'))
                return false;
        } else if (isContainer(name)) {
            if (std::memcmp(name, "fram", 4) == 0)
                sawFrame_ = true;
            if (!walk(value, len, depth + 1))
                return false;
        }
        // Anything else (format string, version, addresses, timecode...) is skipped.
    }
    return true;
}

bool MocopiDecoder::readBone(const uint8_t* data, size_t size, bool definition)
{
    int id = -1, parent = -2;
    const uint8_t* tran = nullptr;
    size_t pos = 0;
    while (pos < size) {
        if (size - pos < 8)
            return false;
        const uint32_t len = readU32(data + pos);
        const char* name = reinterpret_cast<const char*>(data + pos + 4);
        pos += 8;
        if (len > size - pos)
            return false;
        const uint8_t* value = data + pos;
        pos += len;
        if (std::memcmp(name, "bnid", 4) == 0) {
            if (!readInt(value, len, id))
                return false;
        } else if (std::memcmp(name, "pbid", 4) == 0) {
            if (!readInt(value, len, parent))
                return false;
        } else if (std::memcmp(name, "tran", 4) == 0) {
            if (len < 28)
                return false;
            tran = value;
        }
    }
    if (id < 0 || id >= kJointCount || !tran)
        return true; // a bone we don't use; not an error

    Joint& j = joints_[id];
    j.rotation = {readF32(tran), readF32(tran + 4), readF32(tran + 8), readF32(tran + 12)};
    j.position = {readF32(tran + 16), readF32(tran + 20), readF32(tran + 24)};
    if (definition) {
        // Trust the stream's hierarchy only if it can't loop back on itself.
        if (parent >= -1 && parent < kJointCount && parent != id)
            j.parent = parent;
        skeletonReceived_ = true;
    }
    return true;
}

TrackingFrame MocopiDecoder::toFrame() const
{
    // World transforms down the hierarchy (local = translate * rotate).
    std::array<Vec3, kJointCount> pos{};
    std::array<Quat, kJointCount> rot{};
    std::array<int8_t, kJointCount> state{}; // 0 unvisited, 1 visiting, 2 done
    auto resolve = [&](auto& self, int i) -> bool {
        if (state[i] == 2)
            return true;
        if (state[i] == 1)
            return false; // cycle
        state[i] = 1;
        const Joint& j = joints_[i];
        if (j.parent < 0) {
            pos[i] = j.position;
            rot[i] = j.rotation;
        } else {
            if (!self(self, j.parent))
                return false;
            pos[i] = pos[j.parent] + rot[j.parent].rotate(j.position);
            rot[i] = rot[j.parent] * j.rotation;
        }
        state[i] = 2;
        return true;
    };

    TrackingFrame frame;
    for (const RoleJoint& rj : kRoleJoints)
        if (resolve(resolve, rj.joint))
            frame[rj.role] = {true, pos[rj.joint], rot[rj.joint]};
    return frame;
}

std::string MocopiDecoder::summary() const
{
    return std::string(skeletonReceived_ ? "skeleton received" : "waiting for skeleton definition (using default)") +
           " · " + std::to_string(frames_) + " frames";
}

} // namespace mvr::mocopi
