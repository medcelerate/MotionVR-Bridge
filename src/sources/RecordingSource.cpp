#include "sources/RecordingSource.h"

#include "record/Playback.h"
#include "record/RecordingManager.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace mvr {

namespace {

// Recordings in the current folder, newest first.
std::vector<std::string> listRecordings()
{
    std::vector<std::pair<fs::file_time_type, std::string>> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(record::RecordingManager::currentFolder(), ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == ".mvb")
            found.emplace_back(entry.last_write_time(ec), entry.path().filename().string());
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> names;
    for (auto& [time, name] : found)
        names.push_back(std::move(name));
    return names;
}

std::string clock(double seconds)
{
    char buf[16];
    const int s = static_cast<int>(seconds);
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

} // namespace

RecordingSource::~RecordingSource()
{
    stop();
}

Config RecordingSource::defaultConfig() const
{
    const std::vector<std::string> takes = listRecordings();
    return {
        {"file", "Take", ConfigField::Kind::Choice, takes.empty() ? "" : takes.front(),
         takes.empty() ? "No recordings yet in " + record::RecordingManager::currentFolder() : "", takes},
        {"loop", "Loop", ConfigField::Kind::Bool, "true", ""},
        {"speed", "Speed", ConfigField::Kind::Text, "1.0", "0.5 = half speed"},
        {"interpolate", "Interpolate", ConfigField::Kind::Bool, "true",
         "Blend between recorded frames for smooth slow motion and steady output"},
        {"rate", "Output rate (Hz)", ConfigField::Kind::Number, "90", "Used when interpolating"},
    };
}

bool RecordingSource::start(const Config& cfg, FrameHandler onFrame, std::string& error)
{
    stop();
    const std::string file = configString(cfg, "file");
    if (file.empty()) {
        error = "No recording selected";
        return false;
    }
    record::RecordingInfo info;
    std::vector<record::RecordedFrame> frames;
    if (!record::readRecording((fs::path(record::RecordingManager::currentFolder()) / file).string(), info, frames,
                               error))
        return false;
    if (frames.empty()) {
        error = "That recording has no frames";
        return false;
    }
    const float speed = configFloat(cfg, "speed", 1.0f);
    if (!(speed > 0.0f && speed <= 16.0f)) {
        error = "Speed must be between 0 and 16";
        return false;
    }
    const bool loop = configBool(cfg, "loop", true);
    const bool interpolate = configBool(cfg, "interpolate", true);
    const int rate = std::clamp(configInt(cfg, "rate", 90), 1, 1000);

    running_ = true;
    thread_ = std::thread([this, playback = record::Playback(std::move(frames)), onFrame = std::move(onFrame), speed,
                           loop, interpolate, rate, take = info.take.empty() ? file : info.take] {
        using Clock = std::chrono::steady_clock;
        const double duration = static_cast<double>(playback.durationUs());
        Clock::time_point start = Clock::now();
        size_t lastIndex = SIZE_MAX;
        bool finished = false;
        // Ticks run on a fixed schedule so the output rate doesn't drift.
        const auto period = std::chrono::microseconds(interpolate ? 1000000 / rate : 1000);
        auto nextTick = Clock::now();
        while (running_) {
            double t = std::chrono::duration<double, std::micro>(Clock::now() - start).count() * speed;
            if (t > duration) {
                if (loop && duration > 0) {
                    start = Clock::now();
                    t = 0;
                    lastIndex = SIZE_MAX;
                } else {
                    t = duration;
                    finished = true;
                }
            }
            if (!finished) {
                if (interpolate) {
                    onFrame(playback.sample(t, true));
                } else if (const size_t i = playback.indexAt(t); i != lastIndex) {
                    lastIndex = i; // new recorded frame(s) due: send only the latest
                    onFrame(playback.frame(i).frame);
                }
            }
            setStatus((finished ? "Finished " : "Playing ") + take + " · " + clock(t / 1e6) + " / " +
                      clock(duration / 1e6) + (speed != 1.0f ? " · " + std::to_string(speed).substr(0, 4) + "×" : ""));
            nextTick += period;
            if (nextTick < Clock::now())
                nextTick = Clock::now(); // fell behind: don't burst to catch up
            std::this_thread::sleep_until(nextTick);
        }
    });
    return true;
}

void RecordingSource::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

std::string RecordingSource::status() const
{
    std::lock_guard lock(mutex_);
    return running_ ? status_ : "Stopped";
}

void RecordingSource::setStatus(std::string s)
{
    std::lock_guard lock(mutex_);
    status_ = std::move(s);
}

} // namespace mvr
