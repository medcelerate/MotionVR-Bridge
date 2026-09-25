#include "net/OscMessage.h"

#include <cstring>

namespace mvr::osc {

namespace {

void putString(std::vector<uint8_t>& out, const std::string& s)
{
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
    while (out.size() % 4)
        out.push_back(0);
}

void putU32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>(v >> shift));
}

uint32_t getU32(const uint8_t* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

// Reads a padded OSC string at `pos`; returns false if it runs past `end`.
bool getString(const uint8_t* data, size_t end, size_t& pos, std::string& out)
{
    const uint8_t* start = data + pos;
    const void* nul = std::memchr(start, 0, end - pos);
    if (!nul)
        return false;
    const size_t len = static_cast<const uint8_t*>(nul) - start;
    out.assign(reinterpret_cast<const char*>(start), len);
    pos += (len + 4) & ~size_t(3);
    return pos <= end;
}

void decodeInto(const uint8_t* data, size_t size, std::vector<Message>& out, int depth)
{
    if (size < 4 || size % 4 || depth > 4)
        return;
    size_t pos = 0;
    std::string address;
    if (!getString(data, size, pos, address))
        return;

    if (address == "#bundle") {
        pos += 8; // time tag
        while (pos + 4 <= size) {
            const uint32_t len = getU32(data + pos);
            pos += 4;
            if (len > size - pos)
                return;
            decodeInto(data + pos, len, out, depth + 1);
            pos += len;
        }
        return;
    }
    if (address.empty() || address[0] != '/')
        return;

    Message m;
    m.address = address;
    std::string tags;
    if (pos < size && !getString(data, size, pos, tags))
        return;
    for (size_t t = 1; t < tags.size(); ++t) { // tags[0] is ','
        const char tag = tags[t];
        if (tag == 'i' || tag == 'f') {
            if (pos + 4 > size)
                break;
            const uint32_t bits = getU32(data + pos);
            pos += 4;
            if (tag == 'i') {
                m.arguments.emplace_back(static_cast<int32_t>(bits));
            } else {
                float f;
                std::memcpy(&f, &bits, sizeof(f));
                m.arguments.emplace_back(f);
            }
        } else if (tag == 's') {
            std::string s;
            if (!getString(data, size, pos, s))
                break;
            m.arguments.emplace_back(std::move(s));
        } else if (tag == 'T' || tag == 'F') {
            m.arguments.emplace_back(tag == 'T');
        } else {
            break;
        }
    }
    out.push_back(std::move(m));
}

} // namespace

float Message::number(size_t i, float fallback) const
{
    if (i >= arguments.size())
        return fallback;
    const Argument& a = arguments[i];
    if (auto v = std::get_if<int32_t>(&a))
        return static_cast<float>(*v);
    if (auto v = std::get_if<float>(&a))
        return *v;
    if (auto v = std::get_if<bool>(&a))
        return *v ? 1.0f : 0.0f;
    return fallback;
}

std::string Message::text(size_t i) const
{
    if (i < arguments.size())
        if (auto v = std::get_if<std::string>(&arguments[i]))
            return *v;
    return "";
}

std::vector<uint8_t> encode(const Message& message)
{
    std::vector<uint8_t> out;
    putString(out, message.address);
    std::string tags = ",";
    for (const Argument& a : message.arguments) {
        if (std::holds_alternative<int32_t>(a))
            tags += 'i';
        else if (std::holds_alternative<float>(a))
            tags += 'f';
        else if (std::holds_alternative<std::string>(a))
            tags += 's';
        else
            tags += std::get<bool>(a) ? 'T' : 'F';
    }
    putString(out, tags);
    for (const Argument& a : message.arguments) {
        if (auto v = std::get_if<int32_t>(&a)) {
            putU32(out, static_cast<uint32_t>(*v));
        } else if (auto v = std::get_if<float>(&a)) {
            uint32_t bits;
            std::memcpy(&bits, v, sizeof(bits));
            putU32(out, bits);
        } else if (auto v = std::get_if<std::string>(&a)) {
            putString(out, *v);
        }
    }
    return out;
}

std::vector<Message> decode(const uint8_t* data, size_t size)
{
    std::vector<Message> out;
    decodeInto(data, size, out, 0);
    return out;
}

} // namespace mvr::osc
