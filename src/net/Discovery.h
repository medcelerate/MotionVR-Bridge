#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct sockaddr;

namespace mvr {

// Finds other MotionVR Bridge instances on the local network with DNS-SD
// (mDNS), advertising this one as "<name>._mvb._udp.local." with its OSC
// control port. Peers are identified by an id in the TXT record so an
// instance never lists itself.
class Discovery {
public:
    struct Peer {
        std::string id;
        std::string name;
        std::string address; // IPv4, taken from the responding packet
        uint16_t port = 0;
    };

    static constexpr const char* kServiceType = "_mvb._udp.local.";

    ~Discovery();

    bool start(const std::string& instanceName, const std::string& id, uint16_t port, std::string& error);
    void stop();
    bool running() const { return running_; }

    std::vector<Peer> peers() const;

private:
    struct Seen {
        Peer peer;
        std::chrono::steady_clock::time_point lastSeen;
    };

    friend struct DiscoveryCallbacks; // mdns.h callback glue, in Discovery.cpp

    void run(int sock);
    void answer(int sock, uint16_t queryId, uint16_t rclass, const ::sockaddr* from, size_t addrlen);

    std::string name_, id_, instance_, host_;
    uint16_t port_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    std::map<std::string, Seen> peers_; // by id
    // Records from one response packet, joined into a peer.
    struct Pending {
        std::string instance, id, address;
        uint16_t port = 0;
    } pending_;
};

} // namespace mvr
