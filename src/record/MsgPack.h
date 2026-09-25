#pragma once

// The subset of MessagePack (https://msgpack.org) recordings need: nil, bool,
// unsigned ints, float32/64, strings, arrays and maps.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace mvr::msgpack {

class Writer {
public:
    void nil() { out_.push_back(0xc0); }
    void boolean(bool v) { out_.push_back(v ? 0xc3 : 0xc2); }
    void uint(uint64_t v);
    void f32(float v);
    void str(const std::string& s);
    void array(uint32_t count);
    void map(uint32_t count);

    const std::vector<uint8_t>& bytes() const { return out_; }
    void clear() { out_.clear(); }

private:
    void be(uint64_t v, int bytes);
    std::vector<uint8_t> out_;
};

struct Value;
using Array = std::vector<Value>;
using Map = std::map<std::string, Value>; // string keys only

struct Value {
    std::variant<std::nullptr_t, bool, uint64_t, int64_t, double, std::string, Array, Map> v = nullptr;

    bool isNil() const { return std::holds_alternative<std::nullptr_t>(v); }
    double number(double fallback = 0) const;
    std::string text() const;
    const Array* array() const { return std::get_if<Array>(&v); }
    const Map* map() const { return std::get_if<Map>(&v); }
    const Value* get(const std::string& key) const;
};

// Reads one value from data[pos..size); advances pos. Returns false on
// malformed or truncated input (pos is then unspecified).
bool read(const uint8_t* data, size_t size, size_t& pos, Value& out);

} // namespace mvr::msgpack
