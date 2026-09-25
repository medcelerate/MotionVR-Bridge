#include "o3ds/O3dsDecoder.h"

#include "core/Fingers.h"
#include "core/JointNames.h"

#include "o3ds_generated.h"

#include <algorithm>
#include <cmath>

namespace mvr::o3ds {

namespace {

namespace D = O3DS::Data;

bool directionVector(D::Direction d, Vec3& out)
{
    switch (d) {
    case D::Direction_Right: out = {1, 0, 0}; return true;
    case D::Direction_Left: out = {-1, 0, 0}; return true;
    case D::Direction_Up: out = {0, 1, 0}; return true;
    case D::Direction_Down: out = {0, -1, 0}; return true;
    case D::Direction_Back: out = {0, 0, 1}; return true;
    case D::Direction_Forward: out = {0, 0, -1}; return true;
    default: return false;
    }
}

float metersPer(D::DistanceUnit unit)
{
    switch (unit) {
    case D::DistanceUnit_Millimeter: return 0.001f;
    case D::DistanceUnit_Centimeter: return 0.01f;
    case D::DistanceUnit_Meter: return 1.0f;
    case D::DistanceUnit_Kilometer: return 1000.0f;
    case D::DistanceUnit_Inch: return 0.0254f;
    case D::DistanceUnit_Foot: return 0.3048f;
    case D::DistanceUnit_Mile: return 1609.344f;
    default: return 0.0f; // unknown or not meaningful for body tracking
    }
}

} // namespace

Decoder::Mat4 Decoder::Mat4::identity()
{
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        r.m[i][i] = 1;
    return r;
}

Decoder::Mat4 Decoder::Mat4::operator*(const Mat4& o) const
{
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r.m[i][j] += m[i][k] * o.m[k][j];
    return r;
}

Decoder::Decoder(Options options) : options_(std::move(options))
{
    conversion_.unitsToMeters = options_.fallbackUnitsToMeters;
    if (options_.fallbackAxes == FallbackAxes::ZUp)
        conversion_.axes = {Vec3{0, 0, -1}, Vec3{1, 0, 0}, Vec3{0, 1, 0}}; // forward, right, up
}

bool Decoder::decode(const uint8_t* data, size_t size, TrackingFrame& frame, std::string& error)
{
    flatbuffers::Verifier verifier(data, size);
    if (!D::VerifySubjectListBuffer(verifier)) {
        error = "Invalid Open3DStream packet";
        return false;
    }
    const D::SubjectList* list = D::GetSubjectList(data);

    // Coordinate context, when the sender provides a complete one.
    std::array<Vec3, 3> axes;
    if (directionVector(list->x_axis(), axes[0]) && directionVector(list->y_axis(), axes[1]) &&
        directionVector(list->z_axis(), axes[2]))
        conversion_.axes = axes;
    if (float m = metersPer(list->distance_unit()); m > 0)
        conversion_.unitsToMeters = m;

    bool structureChanged = false;
    auto define = [&](const D::SubjectData* sd, bool performer) {
        if (!sd || !sd->uuid() || !sd->nodes())
            return;
        Subject s;
        s.name = sd->name() ? sd->name()->str() : "";
        s.performer = performer;
        for (const D::Transform* t : *sd->nodes()) {
            Node n;
            n.parent = t->parent();
            n.name = t->name() ? t->name()->str() : "";
            if (auto v = t->translation())
                n.translation = {v->x(), v->y(), v->z()};
            if (auto v = t->rotation())
                n.rotation = {v->x(), v->y(), v->z(), v->w()};
            if (auto v = t->scale())
                n.scale = {v->x(), v->y(), v->z()};
            if (auto ms = t->matrix())
                for (const D::Matrix* mx : *ms)
                    n.matrices.push_back({{{mx->m00(), mx->m01(), mx->m02(), mx->m03()},
                                           {mx->m10(), mx->m11(), mx->m12(), mx->m13()},
                                           {mx->m20(), mx->m21(), mx->m22(), mx->m23()},
                                           {mx->m30(), mx->m31(), mx->m32(), mx->m33()}}});
            if (auto cs = t->components())
                n.order.assign(cs->begin(), cs->end());
            else
                n.order = {D::Component_Translation, D::Component_Rotation, D::Component_Scale};
            s.nodes.push_back(std::move(n));
        }
        subjects_[sd->uuid()->str()] = std::move(s);
        structureChanged = true;
    };
    auto update = [&](const D::SubjectUpdate* u) {
        if (!u || !u->uuid())
            return;
        auto it = subjects_.find(u->uuid()->str());
        if (it == subjects_.end())
            return; // definition not received yet
        std::vector<Node>& nodes = it->second.nodes;
        auto valid = [&](int i) { return i >= 0 && i < static_cast<int>(nodes.size()); };
        if (u->translation())
            for (const D::TranslationUpdate* t : *u->translation())
                if (valid(t->i()))
                    nodes[t->i()].translation = {t->x(), t->y(), t->z()};
        if (u->rotation())
            for (const D::RotationUpdate* r : *u->rotation())
                if (valid(r->i()))
                    nodes[r->i()].rotation = {r->x(), r->y(), r->z(), r->w()};
        if (u->scale())
            for (const D::ScaleUpdate* s : *u->scale())
                if (valid(s->i()))
                    nodes[s->i()].scale = {s->x(), s->y(), s->z()};
    };

    if (auto ps = list->performers())
        for (const D::Performer* p : *ps)
            define(p->data(), true);
    if (auto rs = list->rigidbodies())
        for (const D::Rigidbody* r : *rs)
            define(r->data(), false);
    if (auto us = list->performer_updates())
        for (const D::PerformerUpdate* u : *us)
            update(u->data());
    if (auto us = list->rigidbody_updates())
        for (const D::RigidbodyUpdate* u : *us)
            update(u->data());

    if (structureChanged)
        rebind();

    frame = {};
    frame.timestampUs = static_cast<uint64_t>(std::max(0.0, list->time()) * 1e6);
    // World transforms are cached per subject so shared ancestors are
    // computed once per packet.
    std::map<std::string, std::pair<std::vector<Mat4>, std::vector<int8_t>>> caches;
    for (int r = 0; r < kRoleCount; ++r) {
        const Binding& b = bindings_[r];
        auto it = subjects_.find(b.uuid);
        if (b.node < 0 || it == subjects_.end())
            continue;
        auto& [cache, state] = caches[b.uuid];
        if (cache.empty()) {
            cache.resize(it->second.nodes.size());
            state.assign(it->second.nodes.size(), 0);
        }
        Mat4 world;
        if (worldTransform(it->second, b.node, cache, state, world))
            frame.poses[r] = toCanonical(world);
    }

    if (auto it = subjects_.find(fingerSubject_); it != subjects_.end()) {
        auto& [cache, state] = caches[fingerSubject_];
        if (cache.empty()) {
            cache.resize(it->second.nodes.size());
            state.assign(it->second.nodes.size(), 0);
        }
        auto position = [&](int node, Vec3& out) {
            Mat4 world;
            if (!worldTransform(it->second, node, cache, state, world))
                return false;
            out = toCanonical(world).position;
            return true;
        };
        for (int h = 0; h < 2; ++h) {
            Vec3 wrist;
            if (wristJoint_[h] < 0 || !position(wristJoint_[h], wrist))
                continue;
            std::array<FingerChain, kFingerCount> chains;
            for (int f = 0; f < kFingerCount; ++f)
                for (int j : fingerJoints_[h][f]) {
                    Vec3 p;
                    if (position(j, p))
                        chains[f].push_back(p);
                }
            frame.fingers[h] = fingerPose(wrist, chains);
        }
    }
    return true;
}

void Decoder::rebind()
{
    bindings_ = {};
    boundPerformer_.clear();
    fingerSubject_.clear();
    boundCount_ = 0;

    // Rigid bodies are matched by subject name; a performer by joint name.
    std::vector<std::string> rigidNames, rigidUuids;
    std::string performerUuid;
    for (const auto& [uuid, s] : subjects_) {
        const std::string name = normalizeJointName(s.name);
        if (!options_.subjectFilter.empty() && s.performer && name != normalizeJointName(options_.subjectFilter))
            continue;
        if (!s.performer || s.nodes.size() == 1) {
            rigidNames.push_back(name);
            rigidUuids.push_back(uuid);
        } else if (performerUuid.empty()) {
            performerUuid = uuid;
            boundPerformer_ = s.name;
        }
    }

    std::vector<std::string> jointNames;
    if (!performerUuid.empty())
        for (const Node& n : subjects_[performerUuid].nodes)
            jointNames.push_back(normalizeJointName(n.name));

    fingerSubject_ = performerUuid;
    fingerJoints_[0] = findFingerJoints(Hand::Left, jointNames);
    fingerJoints_[1] = findFingerJoints(Hand::Right, jointNames);
    wristJoint_ = {findRole(TrackerRole::LeftHand, jointNames), findRole(TrackerRole::RightHand, jointNames)};

    for (int r = 0; r < kRoleCount; ++r) {
        const auto role = static_cast<TrackerRole>(r);
        if (int i = findRole(role, rigidNames); i >= 0) {
            bindings_[r] = {rigidUuids[i], 0};
        } else if (int j = findRole(role, jointNames); j >= 0) {
            bindings_[r] = {performerUuid, j};
        } else {
            continue;
        }
        ++boundCount_;
    }
}

bool Decoder::worldTransform(const Subject& subject, int index, std::vector<Mat4>& cache, std::vector<int8_t>& state,
                             Mat4& out) const
{
    if (index < 0 || index >= static_cast<int>(subject.nodes.size()))
        return false;
    if (state[index] == 2) {
        out = cache[index];
        return true;
    }
    if (state[index] == 1)
        return false; // cycle in the hierarchy
    state[index] = 1;

    const Node& n = subject.nodes[index];
    Mat4 local = Mat4::identity();
    size_t matrixIndex = 0;
    for (int8_t c : n.order) {
        Mat4 m = Mat4::identity();
        if (c == D::Component_Translation) {
            m.m[0][3] = n.translation.x;
            m.m[1][3] = n.translation.y;
            m.m[2][3] = n.translation.z;
        } else if (c == D::Component_Rotation) {
            const Vec3 x = n.rotation.rotate({1, 0, 0}), y = n.rotation.rotate({0, 1, 0}), z = n.rotation.rotate({0, 0, 1});
            m.m[0][0] = x.x; m.m[1][0] = x.y; m.m[2][0] = x.z;
            m.m[0][1] = y.x; m.m[1][1] = y.y; m.m[2][1] = y.z;
            m.m[0][2] = z.x; m.m[1][2] = z.y; m.m[2][2] = z.z;
        } else if (c == D::Component_Scale) {
            m.m[0][0] = n.scale.x;
            m.m[1][1] = n.scale.y;
            m.m[2][2] = n.scale.z;
        } else if (c == D::Component_Matrix && matrixIndex < n.matrices.size()) {
            m = n.matrices[matrixIndex++];
        }
        local = local * m;
    }

    Mat4 parent = Mat4::identity();
    if (n.parent >= 0 && !worldTransform(subject, n.parent, cache, state, parent))
        return false;
    cache[index] = parent * local;
    state[index] = 2;
    out = cache[index];
    return true;
}

TrackerPose Decoder::toCanonical(const Mat4& w) const
{
    TrackerPose p;
    p.valid = true;
    p.position = conversion_.position({w.m[0][3], w.m[1][3], w.m[2][3]});
    p.orientation = conversion_.rotation({Vec3{w.m[0][0], w.m[1][0], w.m[2][0]}, Vec3{w.m[0][1], w.m[1][1], w.m[2][1]},
                                          Vec3{w.m[0][2], w.m[1][2], w.m[2][2]}});
    return p;
}

std::string Decoder::summary() const
{
    std::string s = boundPerformer_.empty() ? std::to_string(subjects_.size()) + " subject(s)"
                                            : "performer " + boundPerformer_;
    return s + " · " + std::to_string(boundCount_) + "/" + std::to_string(kRoleCount) + " points mapped";
}

bool UdpReassembler::add(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
    constexpr size_t kHeader = 16;
    constexpr uint32_t kMaxFrame = 16u << 20;
    if (size < kHeader)
        return false;
    uint32_t h[4];
    for (int i = 0; i < 4; ++i)
        h[i] = uint32_t(data[i * 4]) | uint32_t(data[i * 4 + 1]) << 8 | uint32_t(data[i * 4 + 2]) << 16 |
               uint32_t(data[i * 4 + 3]) << 24;
    const uint32_t frameId = h[0], index = h[1], total = h[2], fragSize = h[3];
    if (total == 0 || total > kMaxFrame || fragSize == 0)
        return false;
    const uint32_t count = (total + fragSize - 1) / fragSize;
    if (index >= count)
        return false;
    const size_t expected = index == count - 1 ? total - size_t(index) * fragSize : fragSize;
    if (size - kHeader != expected)
        return false;

    Pending& p = pending_[frameId];
    if (p.buffer.empty()) {
        p.totalSize = total;
        p.fragmentSize = fragSize;
        p.buffer.resize(total);
        p.have.assign(count, false);
        p.missing = count;
    } else if (p.totalSize != total || p.fragmentSize != fragSize) {
        return false;
    }
    if (!p.have[index]) {
        std::copy(data + kHeader, data + size, p.buffer.begin() + size_t(index) * fragSize);
        p.have[index] = true;
        --p.missing;
    }
    if (p.missing > 0) {
        // Don't let abandoned frames pile up.
        while (pending_.size() > 16)
            pending_.erase(pending_.begin());
        return false;
    }

    out = std::move(p.buffer);
    // This frame and anything older is done.
    pending_.erase(pending_.begin(), pending_.upper_bound(frameId));
    return true;
}

} // namespace mvr::o3ds
