#include "record/RecordingFile.h"

#include "record/MsgPack.h"

#include <iterator>

namespace mvr::record {

namespace {

constexpr const char* kFormat = "MotionVR Bridge recording";
constexpr uint64_t kFileVersion = 1;

std::string compactName(std::string_view name)
{
    std::string s;
    for (char c : name)
        if (c != ' ')
            s += c;
    return s;
}

std::string csvPath(const std::string& path)
{
    const size_t dot = path.rfind('.');
    return (dot == std::string::npos ? path : path.substr(0, dot)) + ".csv";
}

void flush(std::ofstream& f, msgpack::Writer& w)
{
    f.write(reinterpret_cast<const char*>(w.bytes().data()), static_cast<std::streamsize>(w.bytes().size()));
    w.clear();
}

float num(const msgpack::Value& v)
{
    return static_cast<float>(v.number());
}

} // namespace

bool RecordingWriter::open(const std::string& path, const RecordingInfo& info, bool writeCsv, std::string& error)
{
    close();
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_) {
        error = "Cannot create " + path;
        return false;
    }

    msgpack::Writer w;
    w.map(10);
    w.str("format");
    w.str(kFormat);
    w.str("version");
    w.uint(kFileVersion);
    w.str("start_unix_ms");
    w.uint(info.startUnixMs);
    w.str("source");
    w.str(info.source);
    w.str("take");
    w.str(info.take);
    w.str("coordinates");
    w.str("right-handed, +Y up, -Z forward, meters");
    w.str("points");
    w.array(kRoleCount);
    for (int r = 0; r < kRoleCount; ++r)
        w.str(compactName(roleName(static_cast<TrackerRole>(r))));
    w.str("pose");
    w.array(7);
    for (const char* k : {"x", "y", "z", "qx", "qy", "qz", "qw"})
        w.str(k);
    w.str("fingers");
    w.array(kFingerCount);
    for (const char* k : {"thumb", "index", "middle", "ring", "pinky"})
        w.str(k);
    w.str("frame");
    w.array(4);
    for (const char* k : {"time_us", "points", "fingers", "controllers"})
        w.str(k);
    flush(file_, w);

    if (writeCsv) {
        csv_.open(csvPath(path), std::ios::trunc);
        csv_ << "time_s";
        for (int r = 0; r < kRoleCount; ++r) {
            const std::string n = compactName(roleName(static_cast<TrackerRole>(r)));
            for (const char* c : {"_x", "_y", "_z", "_qx", "_qy", "_qz", "_qw"})
                csv_ << ',' << n << c;
        }
        for (const char* hand : {"Left", "Right"})
            for (const char* f : {"Thumb", "Index", "Middle", "Ring", "Pinky"})
                csv_ << ',' << hand << f << "_curl";
        csv_ << '\n';
    }
    return true;
}

void RecordingWriter::write(uint64_t offsetUs, const TrackingFrame& frame)
{
    if (!file_.is_open())
        return;
    msgpack::Writer w;
    w.array(4);
    w.uint(offsetUs);
    w.array(kRoleCount);
    for (const TrackerPose& p : frame.poses) {
        if (!p.valid) {
            w.nil();
            continue;
        }
        w.array(7);
        for (float v : {p.position.x, p.position.y, p.position.z, p.orientation.x, p.orientation.y, p.orientation.z,
                        p.orientation.w})
            w.f32(v);
    }
    w.array(2);
    for (const FingerPose& f : frame.fingers) {
        if (!f.valid) {
            w.nil();
            continue;
        }
        w.array(2);
        for (const auto* values : {&f.curl, &f.splay}) {
            w.array(kFingerCount);
            for (float v : *values)
                w.f32(v);
        }
    }
    w.array(2);
    for (const ControllerInput& c : frame.controllers) {
        w.array(5);
        w.uint(c.buttons);
        for (float v : {c.trigger, c.grip, c.thumbstickX, c.thumbstickY})
            w.f32(v);
    }
    flush(file_, w);
    if (csv_.is_open())
        writeCsvRow(offsetUs, frame);
}

void RecordingWriter::writeCsvRow(uint64_t offsetUs, const TrackingFrame& frame)
{
    csv_ << offsetUs / 1e6;
    for (const TrackerPose& p : frame.poses) {
        if (!p.valid) {
            csv_ << ",,,,,,,";
            continue;
        }
        csv_ << ',' << p.position.x << ',' << p.position.y << ',' << p.position.z << ',' << p.orientation.x << ','
             << p.orientation.y << ',' << p.orientation.z << ',' << p.orientation.w;
    }
    for (const FingerPose& f : frame.fingers)
        for (float c : f.curl) {
            csv_ << ',';
            if (f.valid)
                csv_ << c;
        }
    csv_ << '\n';
}

void RecordingWriter::close()
{
    if (file_.is_open())
        file_.close();
    if (csv_.is_open())
        csv_.close();
}

bool readRecording(const std::string& path, RecordingInfo& info, std::vector<RecordedFrame>& frames, std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    msgpack::Value header;
    const msgpack::Value* format = nullptr;
    if (!msgpack::read(data.data(), data.size(), pos, header) || !(format = header.get("format")) ||
        format->text() != kFormat) {
        error = "Not a MotionVR Bridge recording: " + path;
        return false;
    }
    if (const msgpack::Value* v = header.get("version"); !v || v->number() > kFileVersion) {
        error = "Recording made by a newer MotionVR Bridge: " + path;
        return false;
    }
    auto text = [&](const char* key) {
        const msgpack::Value* v = header.get(key);
        return v ? v->text() : std::string();
    };
    const msgpack::Value* start = header.get("start_unix_ms");
    info.startUnixMs = start ? static_cast<uint64_t>(start->number()) : 0;
    info.source = text("source");
    info.take = text("take");

    frames.clear();
    while (pos < data.size()) {
        msgpack::Value value;
        if (!msgpack::read(data.data(), data.size(), pos, value))
            break; // a cut-off last frame is dropped
        const msgpack::Array* a = value.array();
        if (!a || a->size() < 4)
            continue;
        RecordedFrame rf;
        rf.offsetUs = static_cast<uint64_t>((*a)[0].number());
        if (const msgpack::Array* points = (*a)[1].array())
            for (size_t r = 0; r < points->size() && r < kRoleCount; ++r)
                if (const msgpack::Array* p = (*points)[r].array(); p && p->size() >= 7)
                    rf.frame.poses[r] = {true, {num((*p)[0]), num((*p)[1]), num((*p)[2])},
                                         {num((*p)[3]), num((*p)[4]), num((*p)[5]), num((*p)[6])}};
        if (const msgpack::Array* hands = (*a)[2].array())
            for (size_t h = 0; h < hands->size() && h < 2; ++h) {
                const msgpack::Array* hand = (*hands)[h].array();
                if (!hand || hand->size() < 2 || !(*hand)[0].array() || !(*hand)[1].array())
                    continue;
                FingerPose& fp = rf.frame.fingers[h];
                fp.valid = true;
                const msgpack::Array& curl = *(*hand)[0].array();
                const msgpack::Array& splay = *(*hand)[1].array();
                for (size_t i = 0; i < kFingerCount; ++i) {
                    fp.curl[i] = i < curl.size() ? num(curl[i]) : 0;
                    fp.splay[i] = i < splay.size() ? num(splay[i]) : 0;
                }
            }
        if (const msgpack::Array* controllers = (*a)[3].array())
            for (size_t h = 0; h < controllers->size() && h < 2; ++h)
                if (const msgpack::Array* c = (*controllers)[h].array(); c && c->size() >= 5)
                    rf.frame.controllers[h] = {static_cast<uint32_t>((*c)[0].number()), num((*c)[1]), num((*c)[2]),
                                               num((*c)[3]), num((*c)[4])};
        frames.push_back(std::move(rf));
    }
    return true;
}

} // namespace mvr::record
