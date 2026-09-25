#include "sources/Open3DStreamSource.h"

#include "net/UdpSocket.h"
#include "o3ds/O3dsDecoder.h"

#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pubsub0/sub.h>

#include <chrono>
#include <cstdlib>

namespace mvr {

namespace {

constexpr const char* kModeSubscribe = "NNG Subscribe";
constexpr const char* kModePairClient = "NNG Pair Client";
constexpr const char* kModePairServer = "NNG Pair Server";
constexpr const char* kModePull = "NNG Pipeline Pull";
constexpr const char* kModeUdp = "UDP";

constexpr const char* kAxesYUp = "Y-up (Maya, MotionBuilder)";
constexpr const char* kAxesZUp = "Z-up (Unreal)";

o3ds::Decoder::Options decoderOptions(const Config& cfg)
{
    o3ds::Decoder::Options o;
    o.subjectFilter = configString(cfg, "subject");
    o.fallbackAxes = configString(cfg, "axes") == kAxesZUp ? o3ds::FallbackAxes::ZUp : o3ds::FallbackAxes::YUp;
    o.fallbackUnitsToMeters = configFloat(cfg, "scale", 0.01f);
    return o;
}

// Accepts "udp://host:port", "host:port" or just "port".
bool parseUdpAddress(std::string url, std::string& host, uint16_t& port)
{
    if (url.rfind("udp://", 0) == 0)
        url = url.substr(6);
    const size_t colon = url.rfind(':');
    host = colon == std::string::npos ? "0.0.0.0" : url.substr(0, colon);
    const std::string portStr = colon == std::string::npos ? url : url.substr(colon + 1);
    char* end = nullptr;
    const long p = std::strtol(portStr.c_str(), &end, 10);
    if (portStr.empty() || *end != '\0' || p <= 0 || p > 65535)
        return false;
    port = static_cast<uint16_t>(p);
    return true;
}

} // namespace

Open3DStreamSource::~Open3DStreamSource()
{
    stop();
}

Config Open3DStreamSource::defaultConfig() const
{
    return {
        {"mode", "Protocol", ConfigField::Kind::Choice, kModeSubscribe,
         "Match the sender: Publish → Subscribe, Pair Server ↔ Client, Pipeline Push → Pull",
         {kModeSubscribe, kModePairClient, kModePairServer, kModePull, kModeUdp}},
        {"url", "Address", ConfigField::Kind::Text, "tcp://127.0.0.1:6001",
         "NNG: tcp://host:port. UDP: udp://0.0.0.0:port to listen"},
        {"subject", "Performer", ConfigField::Kind::Text, "", "Empty = first performer plus named rigid bodies"},
        {"axes", "Axes if the stream doesn't say", ConfigField::Kind::Choice, kAxesYUp, "", {kAxesYUp, kAxesZUp}},
        {"scale", "Units to meters if the stream doesn't say", ConfigField::Kind::Text, "0.01", "0.01 = centimeters"},
    };
}

bool Open3DStreamSource::start(const Config& cfg, FrameHandler onFrame, std::string& error)
{
    stop();
    const std::string mode = configString(cfg, "mode", kModeSubscribe);
    const std::string url = configString(cfg, "url");
    if (url.empty()) {
        error = "Enter the Open3DStream address";
        return false;
    }
    if (mode == kModeUdp) {
        std::string host;
        uint16_t port = 0;
        if (!parseUdpAddress(url, host, port)) {
            error = "UDP address must look like udp://0.0.0.0:6001";
            return false;
        }
    } else if (url.rfind("udp://", 0) == 0) {
        error = "NNG needs a tcp:// (or ipc://, ws://) address";
        return false;
    }

    setStatus("Starting…");
    running_ = true;
    if (mode == kModeUdp)
        thread_ = std::thread(&Open3DStreamSource::runUdp, this, cfg, std::move(onFrame));
    else
        thread_ = std::thread(&Open3DStreamSource::runNng, this, cfg, std::move(onFrame));
    return true;
}

void Open3DStreamSource::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

std::string Open3DStreamSource::status() const
{
    std::lock_guard lock(mutex_);
    return running_ ? status_ : "Stopped";
}

void Open3DStreamSource::setStatus(std::string s)
{
    std::lock_guard lock(mutex_);
    status_ = std::move(s);
}

void Open3DStreamSource::runNng(Config cfg, FrameHandler onFrame)
{
    const std::string mode = configString(cfg, "mode", kModeSubscribe);
    const std::string url = configString(cfg, "url");

    nng_socket sock = NNG_SOCKET_INITIALIZER;
    int rv = 0;
    bool listen = false;
    if (mode == kModeSubscribe) {
        rv = nng_sub0_open(&sock);
        if (rv == 0)
            rv = nng_socket_set(sock, NNG_OPT_SUB_SUBSCRIBE, "", 0);
    } else if (mode == kModePull) {
        rv = nng_pull0_open(&sock);
        listen = true;
    } else {
        rv = nng_pair1_open(&sock);
        listen = mode == kModePairServer;
    }
    if (rv == 0)
        rv = nng_socket_set_ms(sock, NNG_OPT_RECVTIMEO, 200); // lets stop() interrupt recv
    if (rv == 0) {
        // Dialing is non-blocking: NNG keeps retrying until the sender appears.
        rv = listen ? nng_listen(sock, url.c_str(), nullptr, 0) : nng_dial(sock, url.c_str(), nullptr, NNG_FLAG_NONBLOCK);
    }
    if (rv != 0) {
        setStatus(mode + " on " + url + " failed: " + nng_strerror(rv));
        nng_close(sock);
        while (running_)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }

    setStatus((listen ? "Listening on " : "Connecting to ") + url + " (" + mode + ")");
    o3ds::Decoder decoder(decoderOptions(cfg));
    std::string error;
    while (running_) {
        void* buf = nullptr;
        size_t size = 0;
        rv = nng_recv(sock, &buf, &size, NNG_FLAG_ALLOC);
        if (rv == NNG_ETIMEDOUT)
            continue;
        if (rv != 0) {
            setStatus(url + ": " + nng_strerror(rv));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        TrackingFrame frame;
        const bool ok = decoder.decode(static_cast<const uint8_t*>(buf), size, frame, error);
        nng_free(buf, size);
        if (!ok) {
            setStatus(url + ": " + error);
            continue;
        }
        setStatus("Receiving from " + url + " · " + decoder.summary());
        if (running_)
            onFrame(frame);
    }
    nng_close(sock);
}

void Open3DStreamSource::runUdp(Config cfg, FrameHandler onFrame)
{
    std::string host;
    uint16_t port = 0;
    parseUdpAddress(configString(cfg, "url"), host, port);
    const std::string where = host + ":" + std::to_string(port);

    UdpSocket socket;
    std::string error;
    if (!socket.bindLocal(host, port, error)) {
        setStatus(error);
        while (running_)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    setStatus("Listening on UDP " + where);

    o3ds::Decoder decoder(decoderOptions(cfg));
    o3ds::UdpReassembler reassembler;
    std::vector<uint8_t> datagram(65536), packet;
    while (running_) {
        const int n = socket.receive(datagram.data(), datagram.size(), 200);
        if (n <= 0 || !reassembler.add(datagram.data(), static_cast<size_t>(n), packet))
            continue;
        TrackingFrame frame;
        if (!decoder.decode(packet.data(), packet.size(), frame, error)) {
            setStatus("UDP " + where + ": " + error);
            continue;
        }
        setStatus("Receiving on UDP " + where + " · " + decoder.summary());
        if (running_)
            onFrame(frame);
    }
}

} // namespace mvr
