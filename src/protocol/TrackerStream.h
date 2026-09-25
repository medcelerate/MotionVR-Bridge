#pragma once

// Wire format between the MotionVR Bridge app and its SteamVR driver.
// One UDP datagram per frame, little-endian, poses in the canonical frame
// (right-handed, +Y up, -Z forward, meters) of core/Tracking.h.
//
//   PacketHeader
//   TrackerEntry     x header.trackerCount
//   ControllerEntry  x header.controllerCount
//   FingerEntry      x header.fingerCount

#include "core/Tracking.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mvr::stream {

constexpr uint32_t kMagic = 0x4252564D; // "MVRB"
constexpr uint16_t kVersion = 3;
constexpr uint16_t kDefaultPort = 39570;

// PacketHeader::flags
constexpr uint32_t kFlagHandControllers = 1u << 0; // publish hands as controllers
constexpr uint32_t kFlagFingers = 1u << 1;         // drive controller hand skeletons from finger data

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t trackerCount;
    uint8_t controllerCount;
    uint8_t fingerCount;
    uint8_t reserved[3];
    uint32_t session;     // changes every time the app starts streaming
    uint32_t sequence;
    uint64_t timestampUs; // source timestamp
    uint32_t flags;
};

struct TrackerEntry {
    uint8_t role;         // mvr::TrackerRole
    uint8_t reserved[3];
    float position[3];
    float orientation[4]; // x, y, z, w
};

struct ControllerEntry {
    uint8_t hand;         // mvr::Hand
    uint8_t reserved[3];
    uint32_t buttons;     // ControllerInput::Button bits
    float trigger;
    float grip;
    float thumbstick[2];
};

struct FingerEntry {
    uint8_t hand;         // mvr::Hand
    uint8_t reserved[3];
    float curl[kFingerCount];
    float splay[kFingerCount];
};
#pragma pack(pop)

constexpr size_t kMaxPacketSize =
    sizeof(PacketHeader) + kRoleCount * sizeof(TrackerEntry) + 2 * (sizeof(ControllerEntry) + sizeof(FingerEntry));

// Serializes the valid poses in `frame`, plus controller input when
// kFlagHandControllers is set and valid finger data when kFlagFingers is set;
// returns the packet size.
inline size_t encode(const TrackingFrame& frame, uint32_t session, uint32_t sequence, uint32_t flags,
                     unsigned char* out)
{
    PacketHeader h{kMagic, kVersion, 0, 0, 0, {}, session, sequence, frame.timestampUs, flags};
    size_t offset = sizeof(PacketHeader);
    for (int r = 0; r < kRoleCount; ++r) {
        const TrackerPose& p = frame.poses[r];
        if (!p.valid)
            continue;
        TrackerEntry e{};
        e.role = static_cast<uint8_t>(r);
        e.position[0] = p.position.x;
        e.position[1] = p.position.y;
        e.position[2] = p.position.z;
        e.orientation[0] = p.orientation.x;
        e.orientation[1] = p.orientation.y;
        e.orientation[2] = p.orientation.z;
        e.orientation[3] = p.orientation.w;
        std::memcpy(out + offset, &e, sizeof(e));
        offset += sizeof(e);
        ++h.trackerCount;
    }
    if (flags & kFlagHandControllers) {
        for (int hand = 0; hand < 2; ++hand) {
            const ControllerInput& in = frame.controllers[hand];
            ControllerEntry e{};
            e.hand = static_cast<uint8_t>(hand);
            e.buttons = in.buttons;
            e.trigger = in.trigger;
            e.grip = in.grip;
            e.thumbstick[0] = in.thumbstickX;
            e.thumbstick[1] = in.thumbstickY;
            std::memcpy(out + offset, &e, sizeof(e));
            offset += sizeof(e);
            ++h.controllerCount;
        }
    }
    if (flags & kFlagFingers) {
        for (int hand = 0; hand < 2; ++hand) {
            const FingerPose& f = frame.fingers[hand];
            if (!f.valid)
                continue;
            FingerEntry e{};
            e.hand = static_cast<uint8_t>(hand);
            std::memcpy(e.curl, f.curl.data(), sizeof(e.curl));
            std::memcpy(e.splay, f.splay.data(), sizeof(e.splay));
            std::memcpy(out + offset, &e, sizeof(e));
            offset += sizeof(e);
            ++h.fingerCount;
        }
    }
    std::memcpy(out, &h, sizeof(h));
    return offset;
}

// Parses a packet; returns false if it is malformed or from another protocol
// version.
inline bool decode(const unsigned char* data, size_t size, PacketHeader& header, TrackingFrame& frame)
{
    if (size < sizeof(PacketHeader))
        return false;
    std::memcpy(&header, data, sizeof(header));
    const size_t trackersEnd = sizeof(PacketHeader) + size_t(header.trackerCount) * sizeof(TrackerEntry);
    const size_t controllersEnd = trackersEnd + size_t(header.controllerCount) * sizeof(ControllerEntry);
    if (header.magic != kMagic || header.version != kVersion ||
        size < controllersEnd + size_t(header.fingerCount) * sizeof(FingerEntry))
        return false;

    frame = {};
    frame.timestampUs = header.timestampUs;
    for (uint8_t i = 0; i < header.trackerCount; ++i) {
        TrackerEntry e;
        std::memcpy(&e, data + sizeof(PacketHeader) + i * sizeof(TrackerEntry), sizeof(e));
        if (e.role >= kRoleCount)
            continue;
        TrackerPose& p = frame.poses[e.role];
        p.valid = true;
        p.position = {e.position[0], e.position[1], e.position[2]};
        p.orientation = {e.orientation[0], e.orientation[1], e.orientation[2], e.orientation[3]};
    }
    for (uint8_t i = 0; i < header.controllerCount; ++i) {
        ControllerEntry e;
        std::memcpy(&e, data + trackersEnd + i * sizeof(ControllerEntry), sizeof(e));
        if (e.hand > 1)
            continue;
        ControllerInput& in = frame.controllers[e.hand];
        in.buttons = e.buttons;
        in.trigger = e.trigger;
        in.grip = e.grip;
        in.thumbstickX = e.thumbstick[0];
        in.thumbstickY = e.thumbstick[1];
    }
    for (uint8_t i = 0; i < header.fingerCount; ++i) {
        FingerEntry e;
        std::memcpy(&e, data + controllersEnd + i * sizeof(FingerEntry), sizeof(e));
        if (e.hand > 1)
            continue;
        FingerPose& f = frame.fingers[e.hand];
        f.valid = true;
        std::memcpy(f.curl.data(), e.curl, sizeof(e.curl));
        std::memcpy(f.splay.data(), e.splay, sizeof(e.splay));
    }
    return true;
}

} // namespace mvr::stream
