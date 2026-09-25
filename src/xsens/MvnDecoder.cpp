#include "xsens/MvnDecoder.h"

#include "core/Fingers.h"
#include "core/JointNames.h"

#include <cstring>

namespace mvr::xsens {

namespace {

uint32_t readU32(const uint8_t* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

float readF32(const uint8_t* p)
{
    const uint32_t bits = readU32(p);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

const char* const kBodyNames[] = {
    "Pelvis", "L5", "L3", "T12", "T8", "Neck", "Head",
    "RightShoulder", "RightUpperArm", "RightForeArm", "RightHand",
    "LeftShoulder", "LeftUpperArm", "LeftForeArm", "LeftHand",
    "RightUpperLeg", "RightLowerLeg", "RightFoot", "RightToe",
    "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToe",
};

const char* const kFingerNames[] = {
    "Carpus",
    "FirstMC", "FirstPP", "FirstDP",
    "SecondMC", "SecondPP", "SecondMP", "SecondDP",
    "ThirdMC", "ThirdPP", "ThirdMP", "ThirdDP",
    "FourthMC", "FourthPP", "FourthMP", "FourthDP",
    "FifthMC", "FifthPP", "FifthMP", "FifthDP",
};
constexpr int kFingerSegmentsPerHand = 20;

} // namespace

std::string segmentName(int id, int bodySegments, int props)
{
    if (id >= 1 && id <= bodySegments && id <= 23)
        return kBodyNames[id - 1];
    if (id > bodySegments && id <= bodySegments + props)
        return "Prop" + std::to_string(id - bodySegments);
    const int finger = id - bodySegments - props - 1;
    if (finger >= 0 && finger < 2 * kFingerSegmentsPerHand)
        return (finger < kFingerSegmentsPerHand ? "Left" : "Right") +
               std::string(kFingerNames[finger % kFingerSegmentsPerHand]);
    return "";
}

MvnDecoder::MvnDecoder(int characterFilter) : characterFilter_(characterFilter)
{
    // MVN: +X forward, +Y left, +Z up, meters.
    conversion_.axes = {Vec3{0, 0, -1}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}};
    conversion_.unitsToMeters = 1.0f;
}

bool MvnDecoder::add(const uint8_t* data, size_t size, TrackingFrame& frame, std::string& error)
{
    if (size < kHeaderSize || std::memcmp(data, "MXTP", 4) != 0)
        return false; // not MVN
    const std::string type(reinterpret_cast<const char*>(data + 4), 2);
    if (type == "01" || type == "03" || type == "05") {
        error = "Set MVN's network streamer pose to \"Position + Quaternion\"";
        return false;
    }
    if (type != "02")
        return false; // meta data, scale, timecode...: not needed

    const uint32_t sampleCounter = readU32(data + 6);
    const uint8_t datagramCounter = data[10];
    const int items = data[11];
    const uint32_t timecode = readU32(data + 12);
    const int character = data[16];
    if (size < kHeaderSize + size_t(items) * kSegmentSize) {
        error = "Truncated MVN datagram";
        return false;
    }
    if (characterFilter_ >= 0 && character != characterFilter_)
        return false;
    if (character_ < 0)
        character_ = character;
    if (character != character_)
        return false; // follow one character

    Sample& sample = pending_[sampleCounter];
    sample.timecodeMs = timecode;
    sample.bodySegments = data[17];
    sample.props = data[18];
    sample.fingers = data[19];
    const uint8_t* p = data + kHeaderSize;
    for (int i = 0; i < items; ++i, p += kSegmentSize) {
        Segment s;
        s.position = {readF32(p + 4), readF32(p + 8), readF32(p + 12)};
        s.rotation = {readF32(p + 20), readF32(p + 24), readF32(p + 28), readF32(p + 16)}; // q1 q2 q3, q0 = w
        sample.segments[static_cast<int>(readU32(p))] = s;
    }

    // The top bit marks the last datagram of a sample.
    if (!(datagramCounter & 0x80)) {
        while (pending_.size() > 8)
            pending_.erase(pending_.begin());
        return false;
    }
    frame = toFrame(sample);
    pending_.erase(pending_.begin(), pending_.upper_bound(sampleCounter));
    return true;
}

TrackingFrame MvnDecoder::toFrame(const Sample& sample)
{
    std::vector<std::string> names;
    std::vector<const Segment*> segments;
    for (const auto& [id, segment] : sample.segments) {
        names.push_back(normalizeJointName(segmentName(id, sample.bodySegments, sample.props)));
        segments.push_back(&segment);
    }

    TrackingFrame frame;
    frame.timestampUs = uint64_t(sample.timecodeMs) * 1000;
    int mapped = 0;
    for (int r = 0; r < kRoleCount; ++r) {
        const int i = findRole(static_cast<TrackerRole>(r), names);
        if (i < 0)
            continue;
        frame.poses[r] = {true, conversion_.position(segments[i]->position), conversion_.rotation(segments[i]->rotation)};
        ++mapped;
    }
    bool fingers = false;
    for (Hand hand : {Hand::Left, Hand::Right}) {
        const TrackerPose& wrist = frame[hand == Hand::Left ? TrackerRole::LeftHand : TrackerRole::RightHand];
        if (!wrist.valid)
            continue;
        const auto joints = findFingerJoints(hand, names);
        std::array<FingerChain, kFingerCount> chains;
        for (int f = 0; f < kFingerCount; ++f)
            for (int i : joints[f])
                chains[f].push_back(conversion_.position(segments[i]->position));
        frame.fingers[static_cast<int>(hand)] = fingerPose(wrist.position, chains);
        fingers = fingers || frame.fingers[static_cast<int>(hand)].valid;
    }
    mappedPoints_ = mapped;
    fingerData_ = fingers;
    return frame;
}

std::string MvnDecoder::summary() const
{
    if (character_ < 0)
        return "waiting for MVN data";
    return "character " + std::to_string(character_) + " · " + std::to_string(mappedPoints_) + "/" +
           std::to_string(kRoleCount) + " points mapped" + (fingerData_ ? " · fingers" : "");
}

} // namespace mvr::xsens
