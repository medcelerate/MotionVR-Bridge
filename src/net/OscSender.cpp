#include "net/OscSender.h"

#include "net/OscMessage.h"

namespace mvr {

bool OscSender::sendVec3(const std::string& address, float x, float y, float z)
{
    const std::vector<uint8_t> packet = osc::encode({address, {x, y, z}});
    return socket_.send(packet.data(), packet.size());
}

} // namespace mvr
