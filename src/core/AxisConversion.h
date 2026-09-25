#pragma once

#include "core/Tracking.h"

#include <array>

namespace mvr {

// Converts poses from a source's coordinate system into the canonical frame
// (+X right, +Y up, +Z back, meters). Works for left-handed sources too.
struct AxisConversion {
    std::array<Vec3, 3> axes{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}; // canonical direction of the source's X, Y, Z
    float unitsToMeters = 1.0f;

    Vec3 position(const Vec3& p) const { return toCanonical(p) * unitsToMeters; }

    // Rotation given as the source-space columns of its rotation matrix
    // (any scale is removed).
    Quat rotation(const std::array<Vec3, 3>& columns) const
    {
        const Vec3 c[3] = {columns[0].normalized(), columns[1].normalized(), columns[2].normalized()};
        auto rotate = [&](const Vec3& v) { return c[0] * v.x + c[1] * v.y + c[2] * v.z; };
        // R' = C R C^T, column by column; proper even for left-handed sources.
        Vec3 basis[3];
        for (int j = 0; j < 3; ++j) {
            const Vec3 sourceAxis{component(axes[0], j), component(axes[1], j), component(axes[2], j)};
            basis[j] = toCanonical(rotate(sourceAxis));
        }
        return Quat::fromBasis(basis[0], basis[1], basis[2]);
    }

    Quat rotation(const Quat& q) const { return rotation({q.rotate({1, 0, 0}), q.rotate({0, 1, 0}), q.rotate({0, 0, 1})}); }

private:
    Vec3 toCanonical(const Vec3& v) const { return axes[0] * v.x + axes[1] * v.y + axes[2] * v.z; }
    static float component(const Vec3& v, int j) { return j == 0 ? v.x : j == 1 ? v.y : v.z; }
};

} // namespace mvr
