#include "net/OscSender.h"

#include <cstring>

namespace mvr {

namespace {

// OSC strings are null-terminated and padded to a multiple of 4 bytes.
size_t writeOscString(char* out, const char* s)
{
    size_t len = std::strlen(s) + 1;
    size_t padded = (len + 3) & ~size_t(3);
    std::memcpy(out, s, len);
    std::memset(out + len, 0, padded - len);
    return padded;
}

size_t writeOscFloat(char* out, float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    out[0] = char(bits >> 24);
    out[1] = char(bits >> 16);
    out[2] = char(bits >> 8);
    out[3] = char(bits);
    return 4;
}

} // namespace

bool OscSender::sendVec3(const std::string& address, float x, float y, float z)
{
    char buf[256];
    if (address.size() + 1 > sizeof(buf) - 32)
        return false;
    size_t n = writeOscString(buf, address.c_str());
    n += writeOscString(buf + n, ",fff");
    n += writeOscFloat(buf + n, x);
    n += writeOscFloat(buf + n, y);
    n += writeOscFloat(buf + n, z);
    return socket_.send(buf, n);
}

} // namespace mvr
