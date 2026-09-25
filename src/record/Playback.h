#pragma once

#include "record/RecordingFile.h"

#include <vector>

namespace mvr::record {

// Samples a recording at any time, optionally interpolating between the
// recorded frames: positions and finger values blend linearly, rotations
// slerp, buttons come from the nearer frame. A point present in only one
// of the two frames isn't blended; the nearer frame decides.
class Playback {
public:
    explicit Playback(std::vector<RecordedFrame> frames);

    bool empty() const { return frames_.empty(); }
    uint64_t durationUs() const { return frames_.empty() ? 0 : frames_.back().offsetUs; }
    size_t frameCount() const { return frames_.size(); }

    // Frame at `timeUs` (clamped to the recording): interpolated, or the
    // latest recorded frame at or before that time.
    TrackingFrame sample(double timeUs, bool interpolate) const;

    // Index of the latest frame at or before `timeUs`.
    size_t indexAt(double timeUs) const;
    const RecordedFrame& frame(size_t i) const { return frames_[i]; }

private:
    std::vector<RecordedFrame> frames_;
};

TrackingFrame blend(const TrackingFrame& a, const TrackingFrame& b, float t);

} // namespace mvr::record
