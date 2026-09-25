#include "StreamReceiver.h"

#include "protocol/TrackerStream.h"

namespace mvr {

StreamReceiver::~StreamReceiver()
{
    stop();
}

bool StreamReceiver::start(const std::string& address, uint16_t port, std::string& error)
{
    stop();
    if (!socket_.bindLocal(address, port, error))
        return false;
    running_ = true;
    thread_ = std::thread(&StreamReceiver::run, this);
    return true;
}

void StreamReceiver::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    socket_.close();
}

StreamReceiver::Latest StreamReceiver::latest() const
{
    std::lock_guard lock(mutex_);
    return latest_;
}

void StreamReceiver::run()
{
    unsigned char buf[2048];
    while (running_) {
        const int n = socket_.receive(buf, sizeof(buf), 100);
        if (n <= 0)
            continue;

        stream::PacketHeader header;
        TrackingFrame frame;
        if (!stream::decode(buf, static_cast<size_t>(n), header, frame))
            continue;

        std::lock_guard lock(mutex_);
        // UDP may reorder; drop anything older than what we already have.
        const bool sameSession = latest_.any && header.session == latest_.session;
        if (sameSession && static_cast<int32_t>(header.sequence - latest_.sequence) <= 0)
            continue;
        latest_.any = true;
        latest_.session = header.session;
        latest_.sequence = header.sequence;
        latest_.flags = header.flags;
        latest_.received = Clock::now();
        latest_.frame = frame;
    }
}

} // namespace mvr
