#include "net/UdpSocket.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketLen = int;
#else
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketLen = socklen_t;
#endif

namespace mvr {

namespace {

#ifdef _WIN32
constexpr uintptr_t kInvalidSocket = INVALID_SOCKET;
struct WsaInit {
    WsaInit()
    {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WsaInit() { WSACleanup(); }
};
#else
constexpr int kInvalidSocket = -1;
#endif

addrinfo* resolve(const std::string& host, uint16_t port, bool passive, std::string& error)
{
    addrinfo hints{};
    hints.ai_family = host.empty() ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    addrinfo* result = nullptr;
    const std::string portStr = std::to_string(port);
    const int rc = getaddrinfo(host.empty() ? nullptr : host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        error = "Cannot resolve " + (host.empty() ? std::string("local address") : host) + ": " + gai_strerror(rc);
        return nullptr;
    }
    return result;
}

} // namespace

UdpSocket::UdpSocket() : sock_(kInvalidSocket)
{
#ifdef _WIN32
    static WsaInit wsa;
#endif
}

UdpSocket::~UdpSocket()
{
    close();
}

bool UdpSocket::connectTo(const std::string& host, uint16_t port, std::string& error)
{
    close();
    addrinfo* ai = resolve(host, port, false, error);
    if (!ai)
        return false;
    sock_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock_ == kInvalidSocket) {
        freeaddrinfo(ai);
        error = "Cannot create UDP socket";
        return false;
    }
    std::memcpy(addr_, ai->ai_addr, ai->ai_addrlen);
    addrLen_ = static_cast<int>(ai->ai_addrlen);
    freeaddrinfo(ai);
    return true;
}

bool UdpSocket::bindLocal(const std::string& address, uint16_t port, std::string& error)
{
    close();
    addrinfo* ai = resolve(address, port, true, error);
    if (!ai)
        return false;
    sock_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock_ == kInvalidSocket) {
        freeaddrinfo(ai);
        error = "Cannot create UDP socket";
        return false;
    }
    const bool ok = ::bind(sock_, ai->ai_addr, static_cast<SocketLen>(ai->ai_addrlen)) == 0;
    freeaddrinfo(ai);
    if (!ok) {
        close();
        error = "Cannot bind UDP port " + std::to_string(port);
        return false;
    }
    addrLen_ = 0;
    return true;
}

void UdpSocket::close()
{
    if (sock_ == kInvalidSocket)
        return;
#ifdef _WIN32
    closesocket(sock_);
#else
    ::close(sock_);
#endif
    sock_ = kInvalidSocket;
}

bool UdpSocket::isOpen() const
{
    return sock_ != kInvalidSocket;
}

bool UdpSocket::send(const void* data, size_t size)
{
    if (sock_ == kInvalidSocket || addrLen_ == 0)
        return false;
    auto sent = sendto(sock_, static_cast<const char*>(data), static_cast<int>(size), 0,
                       reinterpret_cast<const sockaddr*>(addr_), static_cast<SocketLen>(addrLen_));
    return sent == static_cast<decltype(sent)>(size);
}

int UdpSocket::receive(void* buffer, size_t size, int timeoutMs)
{
    if (sock_ == kInvalidSocket)
        return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock_, &fds);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    const int ready = select(static_cast<int>(sock_ + 1), &fds, nullptr, nullptr, &tv);
    if (ready <= 0)
        return ready;
    const auto n = recv(sock_, static_cast<char*>(buffer), static_cast<int>(size), 0);
    return n < 0 ? -1 : static_cast<int>(n);
}

} // namespace mvr
