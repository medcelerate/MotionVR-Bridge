#include "core/Fingers.h"

#include <algorithm>
#include <cmath>

namespace mvr {

namespace {

// Largest bend (degrees) at each successive joint of a finger, used to scale
// the measured bend to a 0..1 curl.
constexpr float kThumbMaxBend[] = {60, 60, 80};
constexpr float kFingerMaxBend[] = {90, 100, 80};

float angleBetween(const Vec3& a, const Vec3& b)
{
    const Vec3 na = a.normalized(), nb = b.normalized();
    return std::acos(std::clamp(na.dot(nb), -1.0f, 1.0f));
}

} // namespace

float fingerCurl(Finger finger, const Vec3& wrist, const FingerChain& joints)
{
    if (joints.size() < 2)
        return 0.0f;
    const float* maxBend = finger == Finger::Thumb ? kThumbMaxBend : kFingerMaxBend;
    float bend = 0.0f, full = 0.0f;
    Vec3 previous = joints[0] - wrist;
    for (size_t i = 1; i < joints.size() && i <= 3; ++i) {
        const Vec3 segment = joints[i] - joints[i - 1];
        bend += angleBetween(previous, segment);
        full += maxBend[i - 1] * kDegToRad;
        previous = segment;
    }
    return std::clamp(bend / full, 0.0f, 1.0f);
}

FingerPose fingerPose(const Vec3& wrist, const std::array<FingerChain, kFingerCount>& chains)
{
    FingerPose pose;
    for (int f = 0; f < kFingerCount; ++f) {
        if (chains[f].size() < 2)
            continue;
        pose.curl[f] = fingerCurl(static_cast<Finger>(f), wrist, chains[f]);
        pose.valid = true;
    }
    return pose;
}

std::array<std::vector<int>, kFingerCount> findFingerJoints(Hand hand, const std::vector<std::string>& names)
{
    const std::string side = hand == Hand::Left ? "left" : "right";
    const std::string s = hand == Hand::Left ? "l" : "r";
    static const char* words[kFingerCount] = {"thumb", "index", "middle", "ring", "pinky"};
    static const char* ordinals[kFingerCount] = {"first", "second", "third", "fourth", "fifth"};

    auto indexOf = [&](const std::string& n) {
        auto it = std::find(names.begin(), names.end(), n);
        return it == names.end() ? -1 : static_cast<int>(it - names.begin());
    };

    std::array<std::vector<int>, kFingerCount> result;
    for (int f = 0; f < kFingerCount; ++f) {
        const std::string w = words[f];
        const std::string little = f == 4 ? "little" : w;
        const std::string o = ordinals[f];
        // Candidate naming schemes, each a list of joint names knuckle-first.
        const std::vector<std::vector<std::string>> schemes = {
            {side + "hand" + w + "1", side + "hand" + w + "2", side + "hand" + w + "3", side + "hand" + w + "4"},
            {side + w + "1", side + w + "2", side + w + "3", side + w + "4"},
            {w + "01" + s, w + "02" + s, w + "03" + s},
            {side + little + "proximal", side + little + "intermediate", side + little + "distal"},
            f == 0 ? std::vector<std::string>{side + o + "mc", side + o + "pp", side + o + "dp"}
                   : std::vector<std::string>{side + o + "pp", side + o + "mp", side + o + "dp"},
        };
        for (const auto& scheme : schemes) {
            std::vector<int> found;
            for (const std::string& name : scheme) {
                const int i = indexOf(name);
                if (i < 0)
                    break; // joints must be consecutive from the knuckle
                found.push_back(i);
            }
            if (found.size() >= 2 && found.size() > result[f].size())
                result[f] = std::move(found);
        }
    }
    return result;
}

} // namespace mvr
