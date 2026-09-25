#pragma once

#include "core/Tracking.h"
#include "net/UdpSocket.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace mvr {

// Receives MotionVR Bridge stream packets on a background thread and keeps the
// newest frame.
class StreamReceiver {
public:
    using Clock = std::chrono::steady_clock;

    struct Latest {
        bool any = false;
        uint32_t session = 0;
        uint32_t sequence = 0;
        uint32_t flags = 0; // stream::kFlag*
        Clock::time_point received{};
        TrackingFrame frame;
    };

    ~StreamReceiver();

    bool start(const std::string& address, uint16_t port, std::string& error);
    void stop();
    Latest latest() const;

private:
    void run();

    UdpSocket socket_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    Latest latest_;
};

} // namespace mvr
