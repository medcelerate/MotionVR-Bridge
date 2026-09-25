#include "sources/XsensSource.h"

#include "net/UdpSocket.h"
#include "xsens/MvnDecoder.h"

#include <chrono>
#include <vector>

namespace mvr {

XsensSource::~XsensSource()
{
    stop();
}

Config XsensSource::defaultConfig() const
{
    return {
        {"address", "Listen Address", ConfigField::Kind::Text, "0.0.0.0",
         "0.0.0.0 accepts MVN on another machine"},
        {"port", "Port", ConfigField::Kind::Number, "9763",
         "In MVN's Network Streamer: this PC's IP and port, pose \"Position + Quaternion\""},
        {"character", "Character", ConfigField::Kind::Text, "", "Empty = first character streamed"},
    };
}

bool XsensSource::start(const Config& cfg, FrameHandler onFrame, std::string& error)
{
    stop();
    const int port = configInt(cfg, "port", 9763);
    if (port <= 0 || port > 65535) {
        error = "Invalid port";
        return false;
    }
    const std::string character = configString(cfg, "character");
    setStatus("Starting…");
    running_ = true;
    thread_ = std::thread(&XsensSource::run, this, configString(cfg, "address", "0.0.0.0"),
                          static_cast<uint16_t>(port), character.empty() ? -1 : configInt(cfg, "character", -1),
                          std::move(onFrame));
    return true;
}

void XsensSource::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

std::string XsensSource::status() const
{
    std::lock_guard lock(mutex_);
    return running_ ? status_ : "Stopped";
}

void XsensSource::setStatus(std::string s)
{
    std::lock_guard lock(mutex_);
    status_ = std::move(s);
}

void XsensSource::run(std::string address, uint16_t port, int character, FrameHandler onFrame)
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
    setStatus("Listening on UDP " + where + " for MVN");

    xsens::MvnDecoder decoder(character);
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
        setStatus("Receiving MVN on UDP " + where + " · " + decoder.summary());
        if (running_)
            onFrame(frame);
    }
}

} // namespace mvr
