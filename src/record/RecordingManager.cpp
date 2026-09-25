#include "record/RecordingManager.h"

#include "net/OscMessage.h"
#include "net/UdpSocket.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <random>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace mvr::record {

namespace {

std::mutex g_folderMutex;
std::string g_folder;

std::string timestampTake()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H-%M-%S", &local);
    return buf;
}

std::string hostName()
{
#ifdef _WIN32
    const char* name = std::getenv("COMPUTERNAME");
    return name ? name : "MotionVR Bridge";
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) != 0)
        return "MotionVR Bridge";
    std::string name = buf;
    if (const size_t dot = name.find('.'); dot != std::string::npos)
        name.resize(dot); // "studio.local" -> "studio"
    return name;
#endif
}

uint64_t unixMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

} // namespace

std::string sanitizeTake(const std::string& take)
{
    std::string out;
    for (char c : take) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.' ||
                        c == '(' || c == ')';
        out += ok ? c : '_';
    }
    const size_t first = out.find_first_not_of(". ");
    out = first == std::string::npos ? "" : out.substr(first);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    return out.substr(0, 80);
}

std::string RecordingManager::defaultFolder()
{
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return (fs::path(home ? home : ".") / "Documents" / "MotionVR Bridge" / "Recordings").string();
}

std::string RecordingManager::currentFolder()
{
    std::lock_guard lock(g_folderMutex);
    return g_folder.empty() ? defaultFolder() : g_folder;
}

RecordingManager::RecordingManager()
{
    std::random_device rd;
    char id[17];
    std::snprintf(id, sizeof(id), "%08x%08x", rd(), rd());
    id_ = id;
    settings_.folder = defaultFolder();
    settings_.instanceName = hostName();
}

RecordingManager::~RecordingManager()
{
    stop(Origin::Sync); // don't tell other instances we're quitting
    oscRunning_ = false;
    if (oscThread_.joinable())
        oscThread_.join();
    discovery_.stop();
}

void RecordingManager::applySettings(const RecordingSettings& settings)
{
    bool networkChanged;
    {
        std::lock_guard lock(mutex_);
        networkChanged = settings.oscControl != settings_.oscControl || settings.oscPort != settings_.oscPort ||
                         settings.sync != settings_.sync || settings.instanceName != settings_.instanceName;
        settings_ = settings;
    }
    {
        std::lock_guard lock(g_folderMutex);
        g_folder = settings.folder;
    }
    if (networkChanged)
        restartNetwork();
}

RecordingSettings RecordingManager::settings() const
{
    std::lock_guard lock(mutex_);
    return settings_;
}

void RecordingManager::setSourceName(const std::string& name)
{
    std::lock_guard lock(mutex_);
    sourceName_ = name;
}

void RecordingManager::restartNetwork()
{
    oscRunning_ = false;
    if (oscThread_.joinable())
        oscThread_.join();
    discovery_.stop();

    const RecordingSettings s = settings();
    {
        std::lock_guard lock(mutex_);
        oscError_.clear();
        oscPortRunning_ = 0;
    }
    // Sync needs the listener too: that's how other instances reach us.
    if (!s.oscControl && !s.sync)
        return;
    oscRunning_ = true;
    oscThread_ = std::thread(&RecordingManager::runOsc, this, s.oscPort);
    if (s.sync) {
        std::string error;
        if (!discovery_.start(s.instanceName, id_, s.oscPort, error)) {
            std::lock_guard lock(mutex_);
            oscError_ = error;
        }
    }
}

void RecordingManager::runOsc(uint16_t port)
{
    UdpSocket socket;
    std::string error;
    if (!socket.bindLocal("0.0.0.0", port, error)) {
        std::lock_guard lock(mutex_);
        oscError_ = error;
        return;
    }
    {
        std::lock_guard lock(mutex_);
        oscPortRunning_ = port;
    }
    std::vector<uint8_t> buf(65536);
    while (oscRunning_) {
        const int n = socket.receive(buf.data(), buf.size(), 200);
        if (n <= 0)
            continue;
        for (const osc::Message& m : osc::decode(buf.data(), static_cast<size_t>(n)))
            handle(m);
    }
}

void RecordingManager::handle(const osc::Message& m)
{
    if (m.address == "/mvb/record") {
        m.number(0, 1.0f) >= 0.5f ? (void)start(m.text(1), Origin::Osc) : stop(Origin::Osc);
    } else if (m.address == "/mvb/record/start") {
        start(m.text(0), Origin::Osc);
    } else if (m.address == "/mvb/record/stop") {
        stop(Origin::Osc);
    } else if (m.address == "/mvb/record/toggle") {
        status().recording ? stop(Origin::Osc) : (void)start("", Origin::Osc);
    } else if (m.address == "/mvb/sync/record") {
        if (!settings().sync || m.text(2) == id_)
            return;
        m.number(0) >= 0.5f ? (void)start(m.text(1), Origin::Sync) : stop(Origin::Sync);
    }
}

bool RecordingManager::start(const std::string& requestedTake, Origin origin)
{
    std::string take;
    {
        std::lock_guard lock(mutex_);
        if (recording_)
            return true;
        take = sanitizeTake(requestedTake);
        if (take.empty())
            take = timestampTake();
        std::string base = take;
        if (settings_.sync)
            base += " - " + sanitizeTake(settings_.instanceName);

        std::error_code ec;
        fs::create_directories(settings_.folder, ec);
        fs::path path = fs::path(settings_.folder) / (base + ".mvb");
        for (int i = 2; fs::exists(path); ++i)
            path = fs::path(settings_.folder) / (base + " (" + std::to_string(i) + ").mvb");

        std::string error;
        if (!writer_.open(path.string(), {unixMs(), sourceName_, take}, settings_.csv, error)) {
            message_ = error;
            return false;
        }
        recording_ = true;
        startedAt_ = std::chrono::steady_clock::now();
        frames_ = 0;
        file_ = path.string();
        message_ = std::string(origin == Origin::Local ? "Recording" : origin == Origin::Osc ? "Recording (OSC)"
                                                                                           : "Recording (synced)") +
                   " to " + path.filename().string();
        currentTake_ = take;
    }
    if (origin != Origin::Sync && settings().sync)
        broadcast(true, take);
    return true;
}

void RecordingManager::stop(Origin origin)
{
    std::string take;
    {
        std::lock_guard lock(mutex_);
        if (!recording_)
            return;
        writer_.close();
        recording_ = false;
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
        char buf[96];
        std::snprintf(buf, sizeof(buf), " (%zu frames, %.1f s)", frames_, seconds);
        message_ = "Saved " + fs::path(file_).filename().string() + buf;
        take = currentTake_;
    }
    if (origin != Origin::Sync && settings().sync)
        broadcast(false, take);
}

void RecordingManager::toggle()
{
    status().recording ? stop() : (void)start();
}

void RecordingManager::onFrame(const TrackingFrame& frame)
{
    std::lock_guard lock(mutex_);
    if (!recording_)
        return;
    const auto offset = std::chrono::steady_clock::now() - startedAt_;
    writer_.write(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(offset).count()), frame);
    ++frames_;
}

void RecordingManager::broadcast(bool recording, const std::string& take)
{
    const std::vector<uint8_t> packet = osc::encode({"/mvb/sync/record", {int32_t(recording ? 1 : 0), take, id_}});
    for (const Discovery::Peer& peer : discovery_.peers()) {
        UdpSocket socket;
        std::string error;
        if (socket.connectTo(peer.address, peer.port, error))
            socket.send(packet.data(), packet.size());
    }
}

RecordingManager::Status RecordingManager::status() const
{
    Status s;
    s.peers = discovery_.peers();
    std::lock_guard lock(mutex_);
    s.recording = recording_;
    s.seconds = recording_ ? std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count() : 0;
    s.frames = frames_;
    s.file = file_;
    s.message = message_;
    if (!oscError_.empty()) {
        s.network = oscError_;
    } else if (oscPortRunning_) {
        s.network = "OSC control on port " + std::to_string(oscPortRunning_);
        if (settings_.sync)
            s.network += " · sync: " + std::to_string(s.peers.size()) + " other instance(s)";
    }
    return s;
}

} // namespace mvr::record
