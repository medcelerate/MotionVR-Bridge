#pragma once

#include "core/AxisConversion.h"
#include "core/Tracking.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mvr::xsens {

// Decodes Xsens MVN "Network Streamer" UDP datagrams (message type 02,
// position + quaternion pose). Each datagram is "MXTP02", a 18-byte header
// {sampleCounter, datagramCounter, itemCount, timecode, characterId,
// bodySegments, props, fingers, reserved, payloadSize}, then per segment
// {id, x, y, z, q0..q3}, all big-endian. MVN is Z-up, right-handed,
// +X forward, meters.
class MvnDecoder {
public:
    // characterFilter < 0 follows the first character seen.
    explicit MvnDecoder(int characterFilter = -1);

    // Returns true when `frame` holds a complete sample.
    bool add(const uint8_t* data, size_t size, TrackingFrame& frame, std::string& error);

    std::string summary() const;

    static constexpr size_t kHeaderSize = 24;
    static constexpr size_t kSegmentSize = 32;

private:
    struct Segment {
        Vec3 position;
        Quat rotation;
    };
    struct Sample {
        uint32_t timecodeMs = 0;
        int bodySegments = 0, props = 0, fingers = 0;
        std::map<int, Segment> segments; // by MVN segment id
    };

    TrackingFrame toFrame(const Sample& sample);

    int characterFilter_;
    int character_ = -1;
    std::map<uint32_t, Sample> pending_; // by sample counter
    AxisConversion conversion_;
    int mappedPoints_ = 0;
    bool fingerData_ = false;
};

// MVN segment name for a segment id, given the stream's body/prop counts
// ("Pelvis", "LeftForeArm", "LeftSecondPP", ...), or "" if unknown.
std::string segmentName(int id, int bodySegments, int props);

} // namespace mvr::xsens
