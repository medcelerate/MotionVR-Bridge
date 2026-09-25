#pragma once

#include "core/AxisConversion.h"
#include "core/Tracking.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mvr::o3ds {

// Axis convention assumed for streams that don't declare their own.
enum class FallbackAxes {
    YUp, // +X right, +Y up, +Z back (Maya, MotionBuilder)
    ZUp, // +X forward, +Y right, +Z up (Unreal, left-handed)
};

// Decodes Open3DStream SubjectList packets into TrackingFrames.
//
// Full subject definitions arrive once (and when the structure changes);
// per-frame updates only carry changed transforms, so the decoder keeps each
// subject's hierarchy and applies updates to it. Joints are matched to body
// points by name: named rigid bodies first, then the first performer (or the
// one named in `subjectFilter`).
class Decoder {
public:
    struct Options {
        std::string subjectFilter;
        FallbackAxes fallbackAxes = FallbackAxes::YUp;
        float fallbackUnitsToMeters = 0.01f; // centimeters
    };

    explicit Decoder(Options options);

    // Returns true if the packet was valid and `frame` holds the current pose.
    bool decode(const uint8_t* data, size_t size, TrackingFrame& frame, std::string& error);

    // e.g. "performer Actor1 · 9/11 points mapped"
    std::string summary() const;

private:
    struct Mat4 {
        float m[4][4];
        static Mat4 identity();
        Mat4 operator*(const Mat4& o) const;
    };

    struct Node {
        int parent = -1;
        std::string name;
        Vec3 translation;
        Quat rotation;
        Vec3 scale{1, 1, 1};
        std::vector<Mat4> matrices;
        std::vector<int8_t> order; // O3DS::Data::Component values
    };

    struct Subject {
        std::string name;
        bool performer = true;
        std::vector<Node> nodes;
    };

    struct Binding {
        std::string uuid;
        int node = -1;
    };

    void rebind();
    bool worldTransform(const Subject& subject, int node, std::vector<Mat4>& cache, std::vector<int8_t>& state,
                        Mat4& out) const;
    TrackerPose toCanonical(const Mat4& world) const;

    Options options_;
    std::map<std::string, Subject> subjects_; // by uuid
    std::array<Binding, kRoleCount> bindings_{};
    std::string boundPerformer_;
    int boundCount_ = 0;
    // Performer finger joints per hand (knuckle first) and the wrist joint.
    std::string fingerSubject_;
    std::array<std::array<std::vector<int>, kFingerCount>, 2> fingerJoints_{};
    std::array<int, 2> wristJoint_{-1, -1};

    AxisConversion conversion_; // sender space -> canonical space
};

// Reassembles Open3DStream UDP fragments: each datagram is a 16-byte
// little-endian header {frameId, fragmentIndex, totalSize, fragmentSize}
// followed by that fragment's bytes.
class UdpReassembler {
public:
    // Returns true when `out` holds a complete frame.
    bool add(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

private:
    struct Pending {
        uint32_t totalSize = 0;
        uint32_t fragmentSize = 0;
        std::vector<uint8_t> buffer;
        std::vector<bool> have;
        size_t missing = 0;
    };
    std::map<uint32_t, Pending> pending_;
};

} // namespace mvr::o3ds
