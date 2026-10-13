#pragma once

#include "core/Tracking.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mvr::mocopi {

// Decodes Sony mocopi's "mocopi (UDP)" stream (default port 12351).
//
// Each datagram is a tree of fields: u32 little-endian length, 4-character
// name, then the value (nested fields for containers). A "skdf" packet
// defines the 27-joint skeleton (bone id, parent id, rest transform); "fram"
// packets carry each bone's local transform as 7 floats: rotation x, y, z, w
// then position x, y, z, in meters, Y-up, right-handed.
//
// The format isn't published by Sony; this follows the community notes at
// github.com/seagetch/mcp-receiver (doc/Protocol.md) and Sony's open-source
// MotionBuilder plugin's use of the values. Unknown fields are skipped.
class MocopiDecoder {
public:
    static constexpr int kJointCount = 27;

    MocopiDecoder();

    // Returns true when `frame` holds a new pose (from a "fram" packet).
    bool add(const uint8_t* data, size_t size, TrackingFrame& frame, std::string& error);

    std::string summary() const;

private:
    struct Joint {
        int parent = -1;
        Vec3 position;  // local, meters
        Quat rotation;  // local
    };

    // Walks the fields in data[0..size); returns false if they're malformed.
    bool walk(const uint8_t* data, size_t size, int depth);
    bool readBone(const uint8_t* data, size_t size, bool definition);
    TrackingFrame toFrame() const;

    std::array<Joint, kJointCount> joints_{};
    bool skeletonReceived_ = false;
    bool sawFrame_ = false; // set while walking a packet
    uint32_t frames_ = 0;
};

} // namespace mvr::mocopi
