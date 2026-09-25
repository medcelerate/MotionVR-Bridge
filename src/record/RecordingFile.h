#pragma once

#include "core/Tracking.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace mvr::record {

// .mvb recording: a stream of MessagePack objects, readable with any
// MessagePack library.
//
//   1. header map: format, version, start_unix_ms, source, take, coordinates,
//      and the order of `points`, `pose` values and `fingers`
//   2. one array per frame:
//        [time_us, points, fingers, controllers]
//        points:      one entry per point: nil or [x, y, z, qx, qy, qz, qw]
//        fingers:     [left, right]: nil or [[curl x5], [splay x5]]
//        controllers: [left, right]: [buttons, trigger, grip, stick_x, stick_y]
//
// Poses are in the app's frame: right-handed, +Y up, -Z forward, meters.
struct RecordingInfo {
    uint64_t startUnixMs = 0; // wall clock at the start, for aligning takes across machines
    std::string source;
    std::string take;
};

struct RecordedFrame {
    uint64_t offsetUs = 0;
    TrackingFrame frame;
};

class RecordingWriter {
public:
    bool open(const std::string& path, const RecordingInfo& info, bool writeCsv, std::string& error);
    void write(uint64_t offsetUs, const TrackingFrame& frame);
    void close();
    bool isOpen() const { return file_.is_open(); }

private:
    void writeCsvRow(uint64_t offsetUs, const TrackingFrame& frame);

    std::ofstream file_;
    std::ofstream csv_;
};

// Reads a whole recording; returns false (with `error`) if it isn't one.
bool readRecording(const std::string& path, RecordingInfo& info, std::vector<RecordedFrame>& frames,
                   std::string& error);

} // namespace mvr::record
