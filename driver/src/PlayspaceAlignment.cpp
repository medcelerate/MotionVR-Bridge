#include "PlayspaceAlignment.h"

namespace mvr {

void PlayspaceAlignment::reset()
{
    samples_.clear();
    hasTranslation_ = false;
    hasYaw_ = false;
    yaw_ = 0.0f;
    translation_ = {};
}

void PlayspaceAlignment::addSample(const Vec3& source, const Vec3& hmd)
{
    if (!samples_.empty() && (source - samples_.back().first).length() < kMinSampleSpacing)
        return;
    samples_.emplace_back(source, hmd);
    if (samples_.size() > kMaxSamples)
        samples_.pop_front();
    solve();
}

Vec3 PlayspaceAlignment::rotate(const Vec3& p) const
{
    const float c = std::cos(yaw_), s = std::sin(yaw_);
    return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}

Vec3 PlayspaceAlignment::apply(const Vec3& p) const
{
    const Vec3 r = rotate(p);
    return {r.x + translation_.x, r.y + translation_.y, r.z + translation_.z};
}

void PlayspaceAlignment::solve()
{
    const float n = static_cast<float>(samples_.size());
    Vec3 pMean, qMean;
    for (const auto& [p, q] : samples_) {
        pMean = {pMean.x + p.x / n, pMean.y + p.y / n, pMean.z + p.z / n};
        qMean = {qMean.x + q.x / n, qMean.y + q.y / n, qMean.z + q.z / n};
    }

    // 2D Procrustes on the horizontal plane: find the yaw maximizing
    // sum(q . R(yaw) p) over centered points.
    float a = 0, b = 0, spread = 0;
    for (const auto& [p, q] : samples_) {
        const float px = p.x - pMean.x, pz = p.z - pMean.z;
        const float qx = q.x - qMean.x, qz = q.z - qMean.z;
        a += qx * px + qz * pz;
        b += qx * pz - qz * px;
        spread += px * px + pz * pz;
    }
    spread = std::sqrt(spread / n);
    if (samples_.size() >= kMinSamplesForYaw && spread >= kMinSpreadForYaw) {
        yaw_ = std::atan2(b, a);
        hasYaw_ = true;
    }

    translation_ = qMean - rotate(pMean);
    hasTranslation_ = true;
}

} // namespace mvr
