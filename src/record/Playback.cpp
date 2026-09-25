#include "record/Playback.h"

#include <algorithm>

namespace mvr::record {

Playback::Playback(std::vector<RecordedFrame> frames) : frames_(std::move(frames))
{
    std::stable_sort(frames_.begin(), frames_.end(),
                     [](const RecordedFrame& a, const RecordedFrame& b) { return a.offsetUs < b.offsetUs; });
    // Play from the first frame, even if recording started before data arrived.
    if (!frames_.empty()) {
        const uint64_t first = frames_.front().offsetUs;
        for (RecordedFrame& f : frames_)
            f.offsetUs -= first;
    }
}

size_t Playback::indexAt(double timeUs) const
{
    auto it = std::upper_bound(frames_.begin(), frames_.end(), timeUs,
                               [](double t, const RecordedFrame& f) { return t < static_cast<double>(f.offsetUs); });
    return it == frames_.begin() ? 0 : static_cast<size_t>(it - frames_.begin()) - 1;
}

TrackingFrame Playback::sample(double timeUs, bool interpolate) const
{
    if (frames_.empty())
        return {};
    const size_t i = indexAt(timeUs);
    if (!interpolate || i + 1 >= frames_.size())
        return frames_[i].frame;
    const RecordedFrame& a = frames_[i];
    const RecordedFrame& b = frames_[i + 1];
    const double span = static_cast<double>(b.offsetUs - a.offsetUs);
    const float t = span > 0 ? static_cast<float>(std::clamp((timeUs - a.offsetUs) / span, 0.0, 1.0)) : 0.0f;
    TrackingFrame out = blend(a.frame, b.frame, t);
    out.timestampUs = static_cast<uint64_t>(std::max(0.0, timeUs));
    return out;
}

TrackingFrame blend(const TrackingFrame& a, const TrackingFrame& b, float t)
{
    TrackingFrame out = t < 0.5f ? a : b; // buttons and one-sided data from the nearer frame
    for (int r = 0; r < kRoleCount; ++r) {
        const TrackerPose& pa = a.poses[r];
        const TrackerPose& pb = b.poses[r];
        if (pa.valid && pb.valid)
            out.poses[r] = {true, pa.position + (pb.position - pa.position) * t,
                            Quat::slerp(pa.orientation, pb.orientation, t)};
    }
    for (int h = 0; h < 2; ++h) {
        const FingerPose& fa = a.fingers[h];
        const FingerPose& fb = b.fingers[h];
        if (!fa.valid || !fb.valid)
            continue;
        for (int f = 0; f < kFingerCount; ++f) {
            out.fingers[h].curl[f] = fa.curl[f] + (fb.curl[f] - fa.curl[f]) * t;
            out.fingers[h].splay[f] = fa.splay[f] + (fb.splay[f] - fa.splay[f]) * t;
        }
    }
    return out;
}

} // namespace mvr::record
