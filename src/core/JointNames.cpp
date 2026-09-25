#include "core/JointNames.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace mvr {

std::string normalizeJointName(const std::string& name)
{
    // Drop "namespace:" and Maya-style "|parent|" prefixes.
    const size_t cut = name.find_last_of(":|");
    const std::string_view base = cut == std::string::npos ? std::string_view(name) : std::string_view(name).substr(cut + 1);
    std::string out;
    for (char c : base)
        if (std::isalnum(static_cast<unsigned char>(c)))
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

const std::vector<std::string>& roleAliases(TrackerRole role)
{
    static const std::array<std::vector<std::string>, kRoleCount> aliases = {{
        /* Head       */ {"head", "hmd"},
        /* Chest      */ {"spine3", "spine2", "chest", "upperchest", "thorax", "spine05", "spine04", "spine03", "spine1"},
        /* Hip        */ {"hips", "pelvis", "hip", "waist"},
        /* LeftElbow  */ {"leftforearm", "leftelbow", "leftlowerarm", "lowerarml", "lradius", "lelbow"},
        /* RightElbow */ {"rightforearm", "rightelbow", "rightlowerarm", "lowerarmr", "rradius", "relbow"},
        /* LeftHand   */ {"lefthand", "leftwrist", "handl", "lhand"},
        /* RightHand  */ {"righthand", "rightwrist", "handr", "rhand"},
        /* LeftKnee   */ {"leftleg", "leftknee", "leftlowerleg", "leftshin", "calfl", "ltibia", "lknee"},
        /* RightKnee  */ {"rightleg", "rightknee", "rightlowerleg", "rightshin", "calfr", "rtibia", "rknee"},
        /* LeftFoot   */ {"leftfoot", "leftankle", "footl", "lfoot", "lankle"},
        /* RightFoot  */ {"rightfoot", "rightankle", "footr", "rfoot", "rankle"},
    }};
    return aliases[static_cast<int>(role)];
}

int findRole(TrackerRole role, const std::vector<std::string>& normalizedNames)
{
    for (const std::string& alias : roleAliases(role)) {
        auto it = std::find(normalizedNames.begin(), normalizedNames.end(), alias);
        if (it != normalizedNames.end())
            return static_cast<int>(it - normalizedNames.begin());
    }
    return -1;
}

} // namespace mvr
