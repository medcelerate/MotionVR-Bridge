#pragma once

#include "core/Tracking.h"

#include <array>
#include <string>
#include <vector>

namespace mvr {

// Joint positions of one finger, knuckle first, towards the tip.
using FingerChain = std::vector<Vec3>;

// Curl from joint positions: the sum of the bends along the finger (starting
// with the knuckle relative to the wrist-to-knuckle direction), relative to a
// fully bent finger. Position-based, so it doesn't depend on how a capture
// system orients its finger joints. Needs the wrist and at least two joints.
float fingerCurl(Finger finger, const Vec3& wrist, const FingerChain& joints);

// Curls for all fingers of a hand; valid if at least one finger could be
// measured. Fingers without enough joints stay at 0.
FingerPose fingerPose(const Vec3& wrist, const std::array<FingerChain, kFingerCount>& chains);

// For each finger, the indexes (into `normalizedNames`, see
// normalizeJointName) of its joints, knuckle first. Understands HumanIK /
// Mixamo (LeftHandIndex1..4), Shogun (LeftIndex1..), Unreal (index_01_l..)
// and Xsens MVN (LeftSecondPP..) naming. Fingers not found are empty.
std::array<std::vector<int>, kFingerCount> findFingerJoints(Hand hand, const std::vector<std::string>& normalizedNames);

} // namespace mvr
