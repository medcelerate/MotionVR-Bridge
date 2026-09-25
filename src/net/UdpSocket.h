#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mvr {

// Small cross-platform UDP socket: either a sender bound to one destination
// or a receiver bound to a local port.
class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool connectTo(const std::string& host, uint16_t port, std::string& error);
    bool bindLocal(const std::string& address, uint16_t port, std::string& error);
    void close();
    bool isOpen() const;

    bool send(const void* data, size_t size);
    // Returns the number of bytes received, 0 on timeout, -1 on error.
    int receive(void* buffer, size_t size, int timeoutMs);

private:
#ifdef _WIN32
    uintptr_t sock_;
#else
    int sock_;
#endif
    alignas(8) unsigned char addr_[128];
    int addrLen_ = 0;
};

} // namespace mvr
