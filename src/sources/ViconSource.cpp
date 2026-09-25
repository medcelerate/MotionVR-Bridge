#include "sources/ViconSource.h"

#include "core/JointNames.h"

#include "DataStreamClient.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace vds = ViconDataStreamSDK::CPP;

namespace mvr {

namespace {

struct Binding {
    std::string subject;
    std::string segment;
};

using Bindings = std::array<std::optional<Binding>, kRoleCount>;

} // namespace

struct ViconSource::Worker {
    std::string host;
    std::string subjectFilter; // empty = any subject
    float scale = 0.001f;
    FrameHandler onFrame;

    std::atomic<bool> running{true};
    std::mutex mutex;
    std::condition_variable exited;
    bool done = false;
    std::string status = "Starting…";

    void setStatus(std::string s)
    {
        std::lock_guard lock(mutex);
        status = std::move(s);
    }

    void run();
    Bindings bind(vds::Client& client, std::string& summary) const;
};

Bindings ViconSource::Worker::bind(vds::Client& client, std::string& summary) const
{
    // Labels: segment names of multi-segment subjects (skeletons) and the
    // subject name of single-segment subjects (rigid bodies).
    struct Candidate {
        std::string label;
        Binding binding;
        bool skeleton;
    };
    std::vector<Candidate> candidates;
    std::string firstSkeleton;

    const unsigned subjects = client.GetSubjectCount().SubjectCount;
    for (unsigned s = 0; s < subjects; ++s) {
        const std::string subject = client.GetSubjectName(s).SubjectName;
        if (!subjectFilter.empty() && normalizeJointName(subject) != normalizeJointName(subjectFilter))
            continue;
        const unsigned segments = client.GetSegmentCount(subject).SegmentCount;
        if (segments == 1) {
            const std::string segment = client.GetSegmentName(subject, 0).SegmentName;
            candidates.push_back({normalizeJointName(subject), {subject, segment}, false});
            continue;
        }
        // Without a filter, only follow the first skeleton so two performers
        // don't get mixed together.
        if (subjectFilter.empty()) {
            if (!firstSkeleton.empty() && subject != firstSkeleton)
                continue;
            firstSkeleton = subject;
        }
        for (unsigned g = 0; g < segments; ++g) {
            const std::string segment = client.GetSegmentName(subject, g).SegmentName;
            candidates.push_back({normalizeJointName(segment), {subject, segment}, true});
        }
    }

    Bindings bindings;
    int bound = 0;
    for (int r = 0; r < kRoleCount; ++r) {
        for (const std::string& alias : roleAliases(static_cast<TrackerRole>(r))) {
            // Named rigid bodies win over skeleton segments with the same name.
            auto best = std::find_if(candidates.begin(), candidates.end(),
                                     [&](const Candidate& c) { return !c.skeleton && c.label == alias; });
            if (best == candidates.end())
                best = std::find_if(candidates.begin(), candidates.end(),
                                    [&](const Candidate& c) { return c.label == alias; });
            if (best != candidates.end()) {
                bindings[r] = best->binding;
                ++bound;
                break;
            }
        }
    }

    summary = (firstSkeleton.empty() ? std::to_string(subjects) + " subject(s)" : "skeleton " + firstSkeleton) +
              " · " + std::to_string(bound) + "/" + std::to_string(kRoleCount) + " points mapped";
    return bindings;
}

void ViconSource::Worker::run()
{
    vds::Client client;
    Bindings bindings;
    std::string summary;
    unsigned lastSubjectCount = ~0u;
    int framesSinceBind = 0;

    while (running) {
        if (!client.IsConnected().Connected) {
            setStatus("Connecting to " + host + "…");
            client.SetConnectionTimeout(1000);
            if (client.Connect(host).Result != vds::Result::Success) {
                setStatus("Cannot reach Vicon DataStream at " + host + ", retrying");
                for (int i = 0; i < 10 && running; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            client.EnableSegmentData();
            client.SetStreamMode(vds::StreamMode::ServerPush);
            // Vicon is Z-up (+X forward, +Y left); map to +X right, +Y up, +Z back.
            client.SetAxisMapping(vds::Direction::Right, vds::Direction::Up, vds::Direction::Backward);
            lastSubjectCount = ~0u;
        }

        if (client.GetFrame().Result != vds::Result::Success) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (!running)
            break;

        // Re-bind when subjects come or go, and periodically in case one was
        // renamed or re-created.
        const unsigned subjectCount = client.GetSubjectCount().SubjectCount;
        if (subjectCount != lastSubjectCount || ++framesSinceBind >= 300) {
            lastSubjectCount = subjectCount;
            framesSinceBind = 0;
            bindings = bind(client, summary);
            setStatus("Connected to " + host + " · " + summary);
        }

        TrackingFrame frame;
        const double rate = client.GetFrameRate().FrameRateHz;
        if (rate > 0)
            frame.timestampUs = static_cast<uint64_t>(client.GetFrameNumber().FrameNumber * 1e6 / rate);
        for (int r = 0; r < kRoleCount; ++r) {
            if (!bindings[r])
                continue;
            const Binding& b = *bindings[r];
            const auto t = client.GetSegmentGlobalTranslation(b.subject, b.segment);
            const auto q = client.GetSegmentGlobalRotationQuaternion(b.subject, b.segment);
            if (t.Result != vds::Result::Success || q.Result != vds::Result::Success || t.Occluded || q.Occluded)
                continue;
            TrackerPose& p = frame.poses[r];
            p.valid = true;
            p.position = Vec3{float(t.Translation[0]), float(t.Translation[1]), float(t.Translation[2])} * scale;
            p.orientation = {float(q.Rotation[0]), float(q.Rotation[1]), float(q.Rotation[2]), float(q.Rotation[3])};
        }
        if (running)
            onFrame(frame);
    }

    client.Disconnect();
    std::lock_guard lock(mutex);
    done = true;
    exited.notify_all();
}

ViconSource::~ViconSource()
{
    stop();
}

Config ViconSource::defaultConfig() const
{
    return {
        {"host", "DataStream Host", ConfigField::Kind::Text, "localhost:801", "Machine running Shogun / Nexus / Tracker"},
        {"subject", "Subject", ConfigField::Kind::Text, "", "Empty = first skeleton plus named rigid bodies"},
        {"scale", "Units to meters", ConfigField::Kind::Text, "0.001", "Vicon streams millimeters"},
    };
}

bool ViconSource::start(const Config& cfg, FrameHandler onFrame, std::string& error)
{
    stop();
    auto worker = std::make_shared<Worker>();
    worker->host = configString(cfg, "host", "localhost:801");
    worker->subjectFilter = configString(cfg, "subject");
    worker->scale = configFloat(cfg, "scale", 0.001f);
    worker->onFrame = std::move(onFrame);
    if (worker->host.empty()) {
        error = "Enter the Vicon DataStream host";
        return false;
    }
    worker_ = worker;
    // The thread keeps its own reference so it can outlive a stalled stop().
    std::thread([worker] { worker->run(); }).detach();
    return true;
}

void ViconSource::stop()
{
    if (!worker_)
        return;
    worker_->running = false;
    // GetFrame() blocks until the server sends something, so give it a moment
    // to notice; a stalled server must not freeze the UI. Frames are no
    // longer delivered either way.
    std::unique_lock lock(worker_->mutex);
    worker_->exited.wait_for(lock, std::chrono::seconds(2), [&] { return worker_->done; });
    lock.unlock();
    worker_.reset();
}

std::string ViconSource::status() const
{
    if (!worker_)
        return "Stopped";
    std::lock_guard lock(worker_->mutex);
    return worker_->status;
}

} // namespace mvr
