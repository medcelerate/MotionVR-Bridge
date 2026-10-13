#include "sources/MocopiSource.h"

#include "net/UdpSocket.h"
#include "mocopi/MocopiDecoder.h"

#include <chrono>
#include <vector>

namespace mvr {

MocopiSource::~MocopiSource()
{
    stop();
}

Config MocopiSource::defaultConfig() const
{
    return {
        {"address", "Listen Address", ConfigField::Kind::Text, "0.0.0.0",
         "0.0.0.0 accepts the phone over Wi-Fi"},
        {"port", "Port", ConfigField::Kind::Number, "12351",
         "In the mocopi app: send to this PC's IP and port, format \"mocopi (UDP)\". One port per performer."},
    };
}

bool MocopiSource::start(const Config& cfg, FrameHandler onFrame, std::string& error)
{
    stop();
    const int port = configInt(cfg, "port", 12351);
    if (port <= 0 || port > 65535) {
        error = "Invalid port";
        return false;
    }
    setStatus("Starting…");
    running_ = true;
    thread_ = std::thread(&MocopiSource::run, this, configString(cfg, "address", "0.0.0.0"),
                          static_cast<uint16_t>(port), std::move(onFrame));
    return true;
}

void MocopiSource::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

std::string MocopiSource::status() const
{
    std::lock_guard lock(mutex_);
    return running_ ? status_ : "Stopped";
}

void MocopiSource::setStatus(std::string s)
{
    std::lock_guard lock(mutex_);
    status_ = std::move(s);
}

void MocopiSource::run(std::string address, uint16_t port, FrameHandler onFrame)
{
    const std::string where = address + ":" + std::to_string(port);
    UdpSocket socket;
    std::string error;
    if (!socket.bindLocal(address, port, error)) {
        setStatus(error);
        while (running_)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    setStatus("Listening on UDP " + where + " for mocopi");

    mocopi::MocopiDecoder decoder;
    std::vector<uint8_t> datagram(65536);
    while (running_) {
        const int n = socket.receive(datagram.data(), datagram.size(), 200);
        if (n <= 0)
            continue;
        TrackingFrame frame;
        error.clear();
        if (!decoder.add(datagram.data(), static_cast<size_t>(n), frame, error)) {
            if (!error.empty())
                setStatus("UDP " + where + ": " + error);
            continue;
        }
        setStatus("Receiving mocopi on UDP " + where + " · " + decoder.summary());
        if (running_)
            onFrame(frame);
    }
}

} // namespace mvr
