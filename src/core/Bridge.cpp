#include "core/Bridge.h"

#include <algorithm>

namespace mvr {

namespace {

constexpr auto kStaleAfter = std::chrono::milliseconds(500);
// Linear speed (m/s) that lights an indicator fully.
constexpr float kFullActivitySpeed = 1.5f;
// Angular speed (rad/s) that lights an indicator fully.
constexpr float kFullActivityAngular = 6.0f;

} // namespace

Bridge::Bridge()
{
    enabled_.set();
}

Bridge::~Bridge()
{
    stop();
}

bool Bridge::start(std::unique_ptr<TrackingSource> source, const Config& sourceCfg,
                   std::unique_ptr<TrackingSink> sink, const Config& sinkCfg, std::string& error)
{
    stop();

    if (!sink->start(sinkCfg, error))
        return false;

    {
        std::lock_guard lock(mutex_);
        lastFrame_ = {};
        lastSeen_ = {};
        activity_ = {};
        frameRate_.reset();
        supported_ = sink->supportedRoles(sinkCfg);
    }

    // The sink must be in place before the source can deliver its first frame.
    sink_ = std::move(sink);
    source_ = std::move(source);
    if (!source_->start(sourceCfg, [this](const TrackingFrame& f) { onFrame(f); }, error)) {
        source_.reset();
        sink_->stop();
        sink_.reset();
        return false;
    }
    return true;
}

void Bridge::stop()
{
    // Source first: once stop() returns no more frames arrive, so the sink
    // can be torn down safely.
    if (source_) {
        source_->stop();
        source_.reset();
    }
    if (sink_) {
        sink_->stop();
        sink_.reset();
    }
}

bool Bridge::running() const
{
    return source_ != nullptr;
}

void Bridge::runSourceAction(const std::string& key)
{
    if (source_)
        source_->runAction(key);
}

void Bridge::runSinkAction(const std::string& key)
{
    if (sink_)
        sink_->runAction(key);
}

void Bridge::setRoleEnabled(TrackerRole role, bool enabled)
{
    std::lock_guard lock(mutex_);
    enabled_.set(static_cast<int>(role), enabled);
}

bool Bridge::roleEnabled(TrackerRole role) const
{
    std::lock_guard lock(mutex_);
    return enabled_.test(static_cast<int>(role));
}

void Bridge::onFrame(const TrackingFrame& frame)
{
    const auto now = Clock::now();
    RoleSet forward;

    {
        std::lock_guard lock(mutex_);
        for (int i = 0; i < kRoleCount; ++i) {
            const TrackerPose& cur = frame.poses[i];
            if (!cur.valid)
                continue;

            const TrackerPose& prev = lastFrame_.poses[i];
            float target = 0.0f;
            if (prev.valid && now - lastSeen_[i] < kStaleAfter) {
                float dt = std::chrono::duration<float>(now - lastSeen_[i]).count();
                if (dt > 1e-4f) {
                    float linear = (cur.position - prev.position).length() / dt;
                    float angular = Quat::angleBetween(cur.orientation, prev.orientation) / dt;
                    target = std::max(linear / kFullActivitySpeed, angular / kFullActivityAngular);
                }
            }
            // Fast attack, slow release so short motions stay visible.
            float a = activity_[i];
            activity_[i] = std::clamp(target > a ? a + (target - a) * 0.5f : a * 0.92f, 0.0f, 1.0f);
            lastSeen_[i] = now;
            lastFrame_.poses[i] = cur;
        }
        forward = enabled_ & supported_;
        frameRate_.add(1, now);
        lastFrameTime_ = now;
    }

    if (tap_)
        tap_(frame);
    sink_->send(frame, forward);
}

Bridge::Snapshot Bridge::snapshot()
{
    Snapshot s;
    s.running = running();
    if (source_)
        s.sourceStatus = source_->status();
    if (sink_)
        s.sinkStatus = sink_->status();

    const auto now = Clock::now();
    std::lock_guard lock(mutex_);
    for (int i = 0; i < kRoleCount; ++i) {
        bool live = s.running && now - lastSeen_[i] < kStaleAfter;
        s.roles[i] = {live, live ? activity_[i] : 0.0f};
    }
    s.frameRate = s.running && now - lastFrameTime_ < std::chrono::seconds(2) ? frameRate_.rate() : 0.0f;
    return s;
}

} // namespace mvr
