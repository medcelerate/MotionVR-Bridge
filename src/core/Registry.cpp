#include "core/Plugin.h"

#include "sinks/SteamVRSink.h"
#include "sinks/VRChatOscSink.h"
#include "sources/CapturySource.h"
#include "sources/TestPatternSource.h"
#ifdef MVR_HAVE_VICON
#include "sources/ViconSource.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace mvr {

namespace {

const ConfigField* findField(const Config& cfg, const std::string& key)
{
    auto it = std::find_if(cfg.begin(), cfg.end(), [&](const ConfigField& f) { return f.key == key; });
    return it == cfg.end() ? nullptr : &*it;
}

} // namespace

std::string configString(const Config& cfg, const std::string& key, const std::string& fallback)
{
    const ConfigField* f = findField(cfg, key);
    return f ? f->value : fallback;
}

int configInt(const Config& cfg, const std::string& key, int fallback)
{
    const ConfigField* f = findField(cfg, key);
    if (!f || f->value.empty())
        return fallback;
    char* end = nullptr;
    long v = std::strtol(f->value.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int>(v) : fallback;
}

float configFloat(const Config& cfg, const std::string& key, float fallback)
{
    const ConfigField* f = findField(cfg, key);
    if (!f || f->value.empty())
        return fallback;
    char* end = nullptr;
    float v = std::strtof(f->value.c_str(), &end);
    return end && *end == '\0' ? v : fallback;
}

bool configBool(const Config& cfg, const std::string& key, bool fallback)
{
    const ConfigField* f = findField(cfg, key);
    return f ? f->value == "true" : fallback;
}

const std::vector<SourceInfo>& availableSources()
{
    static const std::vector<SourceInfo> sources = {
        {"Captury Live", [] { return std::make_unique<CapturySource>(); }},
#ifdef MVR_HAVE_VICON
        {"Vicon DataStream", [] { return std::make_unique<ViconSource>(); }},
#endif
        {"Test Pattern", [] { return std::make_unique<TestPatternSource>(); }},
    };
    return sources;
}

const std::vector<SinkInfo>& availableSinks()
{
    static const std::vector<SinkInfo> sinks = {
        {"VRChat OSC Trackers", [] { return std::make_unique<VRChatOscSink>(); }},
        {"SteamVR Trackers", [] { return std::make_unique<SteamVRSink>(); }},
    };
    return sinks;
}

} // namespace mvr
