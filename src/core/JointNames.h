#pragma once

#include "core/Tracking.h"

#include <string>
#include <vector>

namespace mvr {

// Lower-case, alphanumeric only, without any namespace prefix, so
// "mixamorig:Left_Foot", "left foot" and "LeftFoot" all compare equal.
std::string normalizeJointName(const std::string& name);

// Normalized names accepted for a role, most specific first. Covers Shogun,
// Plug-in Gait, HumanIK/MotionBuilder, Mixamo and Unreal skeletons, plus
// typical rigid-body object names.
const std::vector<std::string>& roleAliases(TrackerRole role);

// Index of the first name in `normalizedNames` that matches `role`, or -1.
int findRole(TrackerRole role, const std::vector<std::string>& normalizedNames);

} // namespace mvr
