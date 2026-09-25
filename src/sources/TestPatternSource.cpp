#include "sources/TestPatternSource.h"

#include <chrono>

namespace mvr {

TestPatternSource::~TestPatternSource()
{
    stop();
}

Config TestPatternSource::defaultConfig() const
{
    return {
        {"rate", "Rate (Hz)", ConfigField::Kind::Number, "60", ""},
        {"speed", "Animation speed", ConfigField::Kind::Text, "1.0", ""},
        {"height", "Body height (m)", ConfigField::Kind::Text, "1.75", ""},
    };
}

bool TestPatternSource::start(const Config& cfg, FrameHandler onFrame, std::string& error)
{
    stop();
    const int rate = configInt(cfg, "rate", 60);
    if (rate < 1 || rate > 1000) {
        error = "Rate must be between 1 and 1000 Hz";
        return false;
    }
    rateHz_ = rate;
    running_ = true;
    thread_ = std::thread(&TestPatternSource::run, this, std::move(onFrame), rate,
                          configFloat(cfg, "speed", 1.0f), configFloat(cfg, "height", 1.75f));
    return true;
}

void TestPatternSource::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

std::string TestPatternSource::status() const
{
    return running_ ? "Generating test motion at " + std::to_string(rateHz_.load()) + " Hz" : "Stopped";
}

void TestPatternSource::run(FrameHandler onFrame, int rateHz, float speed, float height)
{
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::microseconds(1000000 / rateHz);
    const auto start = Clock::now();
    auto next = start;
    const float s = height / 1.75f;
    const Vec3 up{0, 1, 0};

    while (running_) {
        const float t = std::chrono::duration<float>(Clock::now() - start).count() * speed;
        const float step = std::sin(t * 3.0f);          // marching phase
        const float wave = std::sin(t * 5.0f);          // arm wave
        const float turn = 0.4f * std::sin(t * 0.3f);   // slow body yaw
        const Quat yaw = Quat::fromAxisAngle(up, turn);

        // Build the body facing -Z (right is +X), then yaw the whole thing.
        auto place = [&](TrackerRole role, Vec3 local, Quat localRot = {}) {
            const float c = std::cos(turn), sn = std::sin(turn);
            Vec3 p{local.x * c + local.z * sn, local.y, -local.x * sn + local.z * c};
            return std::pair{role, TrackerPose{true, p * s, yaw * localRot}};
        };

        const float bob = 0.02f * std::fabs(step);
        const float lLift = std::fmax(0.0f, step) * 0.18f;
        const float rLift = std::fmax(0.0f, -step) * 0.18f;
        const Quat lThigh = Quat::fromAxisAngle({1, 0, 0}, lLift * 4.0f);
        const Quat rThigh = Quat::fromAxisAngle({1, 0, 0}, rLift * 4.0f);

        TrackingFrame frame;
        frame.timestampUs = static_cast<uint64_t>(t * 1e6f);
        for (auto [role, pose] : {
                 place(TrackerRole::Head, {0, 1.65f + bob, 0}, Quat::fromAxisAngle(up, 0.3f * wave)),
                 place(TrackerRole::Chest, {0, 1.35f + bob, 0}),
                 place(TrackerRole::Hip, {0, 0.98f + bob, 0}),
                 place(TrackerRole::LeftKnee, {-0.1f, 0.52f + lLift, -lLift * 0.8f}, lThigh),
                 place(TrackerRole::RightKnee, {0.1f, 0.52f + rLift, -rLift * 0.8f}, rThigh),
                 place(TrackerRole::LeftFoot, {-0.1f, 0.08f + lLift, -lLift * 0.3f}),
                 place(TrackerRole::RightFoot, {0.1f, 0.08f + rLift, -rLift * 0.3f}),
                 place(TrackerRole::LeftElbow, {-0.32f, 1.2f + bob, 0.05f * step}),
                 place(TrackerRole::LeftHand, {-0.34f, 0.95f + bob, 0.1f * step}),
                 // Right arm raised and waving
                 place(TrackerRole::RightElbow, {0.38f, 1.45f, -0.05f}, Quat::fromAxisAngle({0, 0, 1}, 0.4f * wave)),
                 place(TrackerRole::RightHand, {0.42f + 0.12f * wave, 1.75f, -0.05f}),
             })
            frame[role] = pose;

        // Fingers: a rolling wave on the left hand, open/close on the right.
        for (int f = 0; f < kFingerCount; ++f) {
            frame.fingers[0].curl[f] = 0.5f + 0.5f * std::sin(t * 3.0f + f * 0.7f);
            frame.fingers[1].curl[f] = 0.5f + 0.5f * std::sin(t * 2.0f);
        }
        frame.fingers[0].valid = frame.fingers[1].valid = true;

        onFrame(frame);

        next += period;
        std::this_thread::sleep_until(next);
    }
}

} // namespace mvr
