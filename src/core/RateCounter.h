#pragma once

#include <chrono>

namespace mvr {

// Counts events and reports the average rate over roughly one-second windows.
// Not thread-safe.
class RateCounter {
public:
    using Clock = std::chrono::steady_clock;

    void reset(Clock::time_point now = Clock::now())
    {
        windowStart_ = now;
        count_ = 0;
        rate_ = 0.0f;
    }

    void add(int n = 1, Clock::time_point now = Clock::now())
    {
        count_ += n;
        const float window = std::chrono::duration<float>(now - windowStart_).count();
        if (window >= 1.0f) {
            rate_ = count_ / window;
            count_ = 0;
            windowStart_ = now;
        }
    }

    float rate() const { return rate_; }

private:
    Clock::time_point windowStart_ = Clock::now();
    int count_ = 0;
    float rate_ = 0.0f;
};

} // namespace mvr
