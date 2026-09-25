#include "record/MsgPack.h"

#include <cstring>

namespace mvr::msgpack {

void Writer::be(uint64_t v, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        out_.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

void Writer::uint(uint64_t v)
{
    if (v < 0x80) {
        out_.push_back(static_cast<uint8_t>(v));
    } else if (v <= 0xff) {
        out_.push_back(0xcc);
        be(v, 1);
    } else if (v <= 0xffff) {
        out_.push_back(0xcd);
        be(v, 2);
    } else if (v <= 0xffffffffu) {
        out_.push_back(0xce);
        be(v, 4);
    } else {
        out_.push_back(0xcf);
        be(v, 8);
    }
}

void Writer::f32(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    out_.push_back(0xca);
    be(bits, 4);
}

void Writer::str(const std::string& s)
{
    const size_t n = s.size();
    if (n < 32) {
        out_.push_back(static_cast<uint8_t>(0xa0 | n));
    } else if (n <= 0xff) {
        out_.push_back(0xd9);
        be(n, 1);
    } else if (n <= 0xffff) {
        out_.push_back(0xda);
        be(n, 2);
    } else {
        out_.push_back(0xdb);
        be(n, 4);
    }
    out_.insert(out_.end(), s.begin(), s.end());
}

void Writer::array(uint32_t count)
{
    if (count < 16) {
        out_.push_back(static_cast<uint8_t>(0x90 | count));
    } else if (count <= 0xffff) {
        out_.push_back(0xdc);
        be(count, 2);
    } else {
        out_.push_back(0xdd);
        be(count, 4);
    }
}

void Writer::map(uint32_t count)
{
    if (count < 16) {
        out_.push_back(static_cast<uint8_t>(0x80 | count));
    } else if (count <= 0xffff) {
        out_.push_back(0xde);
        be(count, 2);
    } else {
        out_.push_back(0xdf);
        be(count, 4);
    }
}

double Value::number(double fallback) const
{
    if (auto p = std::get_if<uint64_t>(&v))
        return static_cast<double>(*p);
    if (auto p = std::get_if<int64_t>(&v))
        return static_cast<double>(*p);
    if (auto p = std::get_if<double>(&v))
        return *p;
    if (auto p = std::get_if<bool>(&v))
        return *p ? 1 : 0;
    return fallback;
}

std::string Value::text() const
{
    auto p = std::get_if<std::string>(&v);
    return p ? *p : std::string();
}

const Value* Value::get(const std::string& key) const
{
    const Map* m = map();
    if (!m)
        return nullptr;
    auto it = m->find(key);
    return it == m->end() ? nullptr : &it->second;
}

namespace {

bool readBe(const uint8_t* data, size_t size, size_t& pos, int bytes, uint64_t& out)
{
    if (size - pos < static_cast<size_t>(bytes))
        return false;
    out = 0;
    for (int i = 0; i < bytes; ++i)
        out = out << 8 | data[pos++];
    return true;
}

bool readValue(const uint8_t* data, size_t size, size_t& pos, Value& out, int depth);

bool readArray(const uint8_t* data, size_t size, size_t& pos, uint64_t count, Value& out, int depth)
{
    if (count > size - pos) // every element needs at least one byte
        return false;
    Array a(count);
    for (Value& e : a)
        if (!readValue(data, size, pos, e, depth + 1))
            return false;
    out.v = std::move(a);
    return true;
}

bool readMap(const uint8_t* data, size_t size, size_t& pos, uint64_t count, Value& out, int depth)
{
    if (count > (size - pos) / 2)
        return false;
    Map m;
    for (uint64_t i = 0; i < count; ++i) {
        Value key, value;
        if (!readValue(data, size, pos, key, depth + 1) || !readValue(data, size, pos, value, depth + 1))
            return false;
        m[key.text()] = std::move(value);
    }
    out.v = std::move(m);
    return true;
}

bool readString(const uint8_t* data, size_t size, size_t& pos, uint64_t len, Value& out)
{
    if (len > size - pos)
        return false;
    out.v = std::string(reinterpret_cast<const char*>(data + pos), len);
    pos += len;
    return true;
}

bool readValue(const uint8_t* data, size_t size, size_t& pos, Value& out, int depth)
{
    if (pos >= size || depth > 32)
        return false;
    const uint8_t t = data[pos++];
    uint64_t n = 0;
    if (t <= 0x7f) {
        out.v = uint64_t(t);
        return true;
    }
    if (t >= 0xe0) {
        out.v = int64_t(static_cast<int8_t>(t));
        return true;
    }
    if ((t & 0xf0) == 0x80)
        return readMap(data, size, pos, t & 0x0f, out, depth);
    if ((t & 0xf0) == 0x90)
        return readArray(data, size, pos, t & 0x0f, out, depth);
    if ((t & 0xe0) == 0xa0)
        return readString(data, size, pos, t & 0x1f, out);
    switch (t) {
    case 0xc0: out.v = nullptr; return true;
    case 0xc2: out.v = false; return true;
    case 0xc3: out.v = true; return true;
    case 0xcc: case 0xcd: case 0xce: case 0xcf:
        if (!readBe(data, size, pos, 1 << (t - 0xcc), n))
            return false;
        out.v = n;
        return true;
    case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
        const int bytes = 1 << (t - 0xd0);
        if (!readBe(data, size, pos, bytes, n))
            return false;
        const int shift = 64 - bytes * 8;
        out.v = static_cast<int64_t>(n << shift) >> shift; // sign-extend
        return true;
    }
    case 0xca: {
        if (!readBe(data, size, pos, 4, n))
            return false;
        const uint32_t bits = static_cast<uint32_t>(n);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        out.v = double(f);
        return true;
    }
    case 0xcb: {
        if (!readBe(data, size, pos, 8, n))
            return false;
        double d;
        std::memcpy(&d, &n, sizeof(d));
        out.v = d;
        return true;
    }
    case 0xd9: case 0xda: case 0xdb:
        return readBe(data, size, pos, 1 << (t - 0xd9), n) && readString(data, size, pos, n, out);
    case 0xdc: case 0xdd:
        return readBe(data, size, pos, t == 0xdc ? 2 : 4, n) && readArray(data, size, pos, n, out, depth);
    case 0xde: case 0xdf:
        return readBe(data, size, pos, t == 0xde ? 2 : 4, n) && readMap(data, size, pos, n, out, depth);
    default:
        return false; // bin, ext and the rest aren't used by recordings
    }
}

} // namespace

bool read(const uint8_t* data, size_t size, size_t& pos, Value& out)
{
    return readValue(data, size, pos, out, 0);
}

} // namespace mvr::msgpack
