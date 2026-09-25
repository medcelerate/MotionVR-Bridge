#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace mvr::osc {

// Argument types this app sends and understands: i, f, s, T/F.
using Argument = std::variant<int32_t, float, std::string, bool>;

struct Message {
    std::string address;
    std::vector<Argument> arguments;

    // Argument i as a number (int, float or bool), or `fallback`.
    float number(size_t i, float fallback = 0.0f) const;
    // Argument i as a string, or "".
    std::string text(size_t i) const;
};

std::vector<uint8_t> encode(const Message& message);

// Parses a packet, expanding #bundle contents. Malformed data yields no
// messages; unsupported argument types end that message's arguments.
std::vector<Message> decode(const uint8_t* data, size_t size);

} // namespace mvr::osc
