#pragma once

#include "net/UdpSocket.h"

#include <cstdint>
#include <string>

namespace mvr {

// Minimal UDP OSC client: enough to send float-vector messages.
class OscSender {
public:
    bool open(const std::string& host, uint16_t port, std::string& error) { return socket_.connectTo(host, port, error); }
    void close() { socket_.close(); }
    bool isOpen() const { return socket_.isOpen(); }

    // Sends `address` with the argument type tag ",fff".
    bool sendVec3(const std::string& address, float x, float y, float z);

private:
    UdpSocket socket_;
};

} // namespace mvr
