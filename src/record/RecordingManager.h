#pragma once

#include "core/Tracking.h"
#include "net/Discovery.h"
#include "record/RecordingFile.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mvr::osc {
struct Message;
}

namespace mvr::record {

struct RecordingSettings {
    std::string folder;
    bool csv = false;
    bool oscControl = false;  // listen for /mvb/record... on oscPort
    uint16_t oscPort = 9100;
    bool sync = false;        // discover other instances and start/stop with them
    std::string instanceName; // shown to other instances, added to synced file names
};

// Records the frames passing through the bridge, and lets OSC and other
// instances on the network start and stop recording.
//
// OSC control (UDP, on oscPort, only when oscControl is on):
//   /mvb/record <1|0> [take]         start / stop
//   /mvb/record/start [take]  /mvb/record/stop  /mvb/record/toggle
// Between instances (sync, needs only sync on; the listener runs for either):
// /mvb/sync/record <1|0> <take> <sender id>, sent to every discovered
// instance and never forwarded again.
class RecordingManager {
public:
    enum class Origin { Local, Osc, Sync };

    struct Status {
        bool recording = false;
        double seconds = 0;
        size_t frames = 0;
        std::string file;    // current or last recording
        std::string message; // last event or error
        std::string network; // OSC / sync state
        std::vector<Discovery::Peer> peers;
    };

    RecordingManager();
    ~RecordingManager();

    static std::string defaultFolder();
    // Folder used by the Recording source to list takes.
    static std::string currentFolder();

    void applySettings(const RecordingSettings& settings);
    RecordingSettings settings() const;
    void setSourceName(const std::string& name);

    bool start(const std::string& take = "", Origin origin = Origin::Local);
    void stop(Origin origin = Origin::Local);
    void toggle();

    // Called for every frame from the source thread.
    void onFrame(const TrackingFrame& frame);

    Status status() const;

private:
    void runOsc(uint16_t port);
    void handle(const osc::Message& m);
    void broadcast(bool recording, const std::string& take);
    void restartNetwork();

    mutable std::mutex mutex_;
    RecordingSettings settings_;
    std::string sourceName_;
    std::string id_;
    RecordingWriter writer_;
    bool recording_ = false;
    std::chrono::steady_clock::time_point startedAt_{};
    size_t frames_ = 0;
    std::string file_;
    std::string currentTake_;
    std::string message_;
    std::string oscError_;

    std::thread oscThread_;
    std::atomic<bool> oscRunning_{false};
    uint16_t oscPortRunning_ = 0;
    Discovery discovery_;
};

// Makes a take name safe as a file name: letters, digits, space and
// "-_.()" only, no leading dots, at most 80 characters.
std::string sanitizeTake(const std::string& take);

} // namespace mvr::record
