#include "net/Discovery.h"

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#endif
#include "mdns.h"

#include <cstring>

#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/select.h>
#endif

namespace mvr {

namespace {

constexpr auto kQueryInterval = std::chrono::seconds(3);
constexpr auto kPeerTimeout = std::chrono::seconds(10);

mdns_string_t str(const std::string& s)
{
    return {s.c_str(), s.size()};
}

// DNS labels can't contain dots; keep names readable otherwise.
std::string label(const std::string& name)
{
    std::string out;
    for (char c : name)
        out += (c == '.' || c == '\0') ? '-' : c;
    return out.empty() ? "MotionVR Bridge" : out.substr(0, 60);
}

} // namespace

struct DiscoveryCallbacks {
    static int onRecord(int sock, const ::sockaddr* from, size_t addrlen, mdns_entry_type_t entry,
                        uint16_t queryId, uint16_t rtype, uint16_t rclass, uint32_t, const void* data, size_t size,
                        size_t nameOffset, size_t, size_t recordOffset, size_t recordLength, void* user)
    {
        auto* self = static_cast<Discovery*>(user);
        char nameBuf[256], valueBuf[256];
        size_t offset = nameOffset;
        const mdns_string_t name = mdns_string_extract(data, size, &offset, nameBuf, sizeof(nameBuf));
        const std::string recordName(name.str, name.length);
        const std::string serviceType = Discovery::kServiceType;

        if (entry == MDNS_ENTRYTYPE_QUESTION) {
            if (recordName == serviceType && (rtype == MDNS_RECORDTYPE_PTR || rtype == MDNS_RECORDTYPE_ANY))
                self->answer(sock, queryId, rclass, from, addrlen);
            return 0;
        }

        Discovery::Pending& p = self->pending_;
        if (rtype == MDNS_RECORDTYPE_PTR && recordName == serviceType) {
            const mdns_string_t ptr = mdns_record_parse_ptr(data, size, recordOffset, recordLength, valueBuf,
                                                            sizeof(valueBuf));
            p.instance.assign(ptr.str, ptr.length);
        } else if (rtype == MDNS_RECORDTYPE_SRV && recordName.size() > serviceType.size() &&
                   recordName.compare(recordName.size() - serviceType.size(), serviceType.size(), serviceType) == 0) {
            const mdns_record_srv_t srv = mdns_record_parse_srv(data, size, recordOffset, recordLength, valueBuf,
                                                                sizeof(valueBuf));
            p.port = srv.port;
            if (p.instance.empty())
                p.instance = recordName;
        } else if (rtype == MDNS_RECORDTYPE_TXT) {
            mdns_record_txt_t txt[8];
            const size_t n = mdns_record_parse_txt(data, size, recordOffset, recordLength, txt, 8);
            for (size_t i = 0; i < n; ++i)
                if (std::string(txt[i].key.str, txt[i].key.length) == "id")
                    p.id.assign(txt[i].value.str, txt[i].value.length);
        } else {
            return 0;
        }
        if (from && from->sa_family == AF_INET) {
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in*>(from)->sin_addr, ip, sizeof(ip));
            p.address = ip;
        }
        return 0;
    }
};

Discovery::~Discovery()
{
    stop();
}

bool Discovery::start(const std::string& instanceName, const std::string& id, uint16_t port, std::string& error)
{
    stop();
#ifdef _WIN32
    static const bool wsa = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)wsa;
#endif
    name_ = label(instanceName);
    id_ = id;
    port_ = port;
    instance_ = name_ + "." + kServiceType;
    host_ = name_ + ".local.";

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(MDNS_PORT);
#ifdef __APPLE__
    addr.sin_len = sizeof(addr);
#endif
    const int sock = mdns_socket_open_ipv4(&addr);
    if (sock < 0) {
        error = "Cannot open the mDNS socket (port 5353)";
        return false;
    }
    running_ = true;
    thread_ = std::thread(&Discovery::run, this, sock);
    return true;
}

void Discovery::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    std::lock_guard lock(mutex_);
    peers_.clear();
}

std::vector<Discovery::Peer> Discovery::peers() const
{
    std::lock_guard lock(mutex_);
    std::vector<Peer> out;
    for (const auto& [id, seen] : peers_)
        out.push_back(seen.peer);
    return out;
}

void Discovery::answer(int sock, uint16_t queryId, uint16_t rclass, const ::sockaddr* from, size_t addrlen)
{
    const std::string serviceType = kServiceType;
    mdns_record_t ptr{};
    ptr.name = str(serviceType);
    ptr.type = MDNS_RECORDTYPE_PTR;
    ptr.data.ptr.name = str(instance_);

    mdns_record_t additional[2]{};
    additional[0].name = str(instance_);
    additional[0].type = MDNS_RECORDTYPE_SRV;
    additional[0].data.srv.port = port_;
    additional[0].data.srv.name = str(host_);
    static const std::string idKey = "id";
    additional[1].name = str(instance_);
    additional[1].type = MDNS_RECORDTYPE_TXT;
    additional[1].data.txt.key = str(idKey);
    additional[1].data.txt.value = str(id_);

    alignas(4) char buffer[1024];
    if (rclass & MDNS_UNICAST_RESPONSE)
        mdns_query_answer_unicast(sock, from, addrlen, buffer, sizeof(buffer), queryId, MDNS_RECORDTYPE_PTR,
                                  serviceType.c_str(), serviceType.size(), ptr, nullptr, 0, additional, 2);
    else
        mdns_query_answer_multicast(sock, buffer, sizeof(buffer), ptr, nullptr, 0, additional, 2);
}

void Discovery::run(int sock)
{
    const std::string serviceType = kServiceType;
    alignas(4) char buffer[2048];
    auto lastQuery = std::chrono::steady_clock::time_point{};

    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastQuery >= kQueryInterval) {
            lastQuery = now;
            // Asking also makes every instance (including this one) answer,
            // which doubles as announcing ourselves.
            mdns_query_send(sock, MDNS_RECORDTYPE_PTR, serviceType.c_str(), serviceType.size(), buffer,
                            sizeof(buffer), 0);
            std::lock_guard lock(mutex_);
            for (auto it = peers_.begin(); it != peers_.end();)
                it = now - it->second.lastSeen > kPeerTimeout ? peers_.erase(it) : std::next(it);
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv{0, 200000};
        if (select(sock + 1, &fds, nullptr, nullptr, &tv) <= 0)
            continue;

        pending_ = {};
        mdns_socket_listen(sock, buffer, sizeof(buffer), &DiscoveryCallbacks::onRecord, this);
        if (pending_.id.empty() || pending_.id == id_ || pending_.port == 0 || pending_.address.empty())
            continue;
        Peer peer;
        peer.id = pending_.id;
        peer.name = pending_.instance.substr(0, pending_.instance.find("._mvb."));
        peer.address = pending_.address;
        peer.port = pending_.port;
        std::lock_guard lock(mutex_);
        peers_[peer.id] = {peer, std::chrono::steady_clock::now()};
    }
    mdns_socket_close(sock);
}

} // namespace mvr
