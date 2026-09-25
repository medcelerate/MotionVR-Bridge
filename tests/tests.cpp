// Minimal self-checking tests for the pieces that don't need SteamVR or a UI.

#include "PlayspaceAlignment.h"
#include "core/Fingers.h"
#include "core/HandCalibration.h"
#include "core/JointNames.h"
#include "o3ds/O3dsDecoder.h"
#include "o3ds_generated.h"
#include "StreamReceiver.h"
#include "net/UdpSocket.h"
#include "protocol/TrackerStream.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}

bool near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

void testProtocolRoundTrip()
{
    mvr::TrackingFrame in;
    in.timestampUs = 123456789;
    in[mvr::TrackerRole::Hip] = {true, {0.1f, 1.0f, -0.2f}, {0, 0.7071f, 0, 0.7071f}};
    in[mvr::TrackerRole::LeftFoot] = {true, {-0.1f, 0.05f, 0.3f}, {}};

    in.controllers[1].buttons = mvr::ControllerInput::TriggerClick | mvr::ControllerInput::A;
    in.controllers[1].grip = 0.75f;
    in.controllers[1].thumbstickX = -0.5f;

    unsigned char buf[mvr::stream::kMaxPacketSize];
    const size_t size = mvr::stream::encode(in, 42, 7, mvr::stream::kFlagHandControllers, buf);

    mvr::stream::PacketHeader header;
    mvr::TrackingFrame out;
    check(mvr::stream::decode(buf, size, header, out), "protocol: decodes");
    check(header.session == 42 && header.sequence == 7 && header.trackerCount == 2 && header.controllerCount == 2 &&
              header.flags == mvr::stream::kFlagHandControllers,
          "protocol: header fields");
    const mvr::ControllerInput& right = out.controllers[1];
    check(right.pressed(mvr::ControllerInput::TriggerClick) && right.pressed(mvr::ControllerInput::A) &&
              !right.pressed(mvr::ControllerInput::B) && near(right.grip, 0.75f, 1e-6f) &&
              near(right.thumbstickX, -0.5f, 1e-6f),
          "protocol: controller input");

    check(!mvr::stream::decode(buf, size - 1, header, out), "protocol: rejects truncated packet");

    const size_t noHands = mvr::stream::encode(in, 42, 8, 0, buf);
    check(mvr::stream::decode(buf, noHands, header, out) && header.controllerCount == 0 &&
              out.controllers[1].buttons == 0,
          "protocol: controller input omitted without flag");
    check(out.timestampUs == in.timestampUs, "protocol: timestamp");
    check(out[mvr::TrackerRole::Hip].valid && near(out[mvr::TrackerRole::Hip].orientation.y, 0.7071f, 1e-6f),
          "protocol: hip pose");
    check(!out[mvr::TrackerRole::Chest].valid, "protocol: absent roles stay invalid");
}

void testAlignmentRecoversYawAndTranslation()
{
    const float trueYaw = 40.0f * mvr::kDegToRad;
    const mvr::Vec3 trueT{1.5f, -0.3f, -2.0f};
    const float c = std::cos(trueYaw), s = std::sin(trueYaw);
    auto toHmd = [&](mvr::Vec3 p) {
        return mvr::Vec3{p.x * c + p.z * s + trueT.x, p.y + trueT.y, -p.x * s + p.z * c + trueT.z};
    };

    std::mt19937 rng(1);
    std::normal_distribution<float> noise(0.0f, 0.01f);
    mvr::PlayspaceAlignment a;

    // Standing still: translation only.
    for (int i = 0; i < 50; ++i)
        a.addSample({0, 1.7f, 0}, toHmd({0, 1.7f, 0}));
    check(a.hasTranslation() && !a.hasYaw(), "alignment: no yaw while standing still");

    // Walk a loop around the room.
    for (int i = 0; i < 200; ++i) {
        const float t = i * 0.05f;
        const mvr::Vec3 p{1.2f * std::cos(t), 1.7f + 0.02f * std::sin(3 * t), 0.8f * std::sin(t)};
        mvr::Vec3 q = toHmd(p);
        q = {q.x + noise(rng), q.y + noise(rng), q.z + noise(rng)};
        a.addSample(p, q);
    }
    check(a.hasYaw(), "alignment: yaw solved after walking");
    check(near(a.yawRadians(), trueYaw, 1.0f * mvr::kDegToRad), "alignment: yaw within 1 degree");

    const mvr::Vec3 probe{0.5f, 1.0f, -0.4f};
    const mvr::Vec3 err = a.apply(probe) - toHmd(probe);
    check(err.length() < 0.02f, "alignment: mapped point within 2 cm");
}

void testReceiverKeepsNewestPacket()
{
    constexpr uint16_t kPort = 39579;
    mvr::StreamReceiver receiver;
    std::string error;
    check(receiver.start("127.0.0.1", kPort, error), "receiver: binds");

    mvr::UdpSocket sender;
    check(sender.connectTo("127.0.0.1", kPort, error), "receiver: sender connects");

    auto sendFrame = [&](uint32_t session, uint32_t seq, float hipY) {
        mvr::TrackingFrame f;
        f[mvr::TrackerRole::Hip] = {true, {0, hipY, 0}, {}};
        unsigned char buf[mvr::stream::kMaxPacketSize];
        sender.send(buf, mvr::stream::encode(f, session, seq, 0, buf));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    };

    sendFrame(1, 5, 1.0f);
    sendFrame(1, 4, 9.0f); // stale, must be dropped
    auto latest = receiver.latest();
    check(latest.any && latest.sequence == 5 && near(latest.frame[mvr::TrackerRole::Hip].position.y, 1.0f, 1e-6f),
          "receiver: drops out-of-order packet");

    sendFrame(2, 1, 2.0f); // new session restarts sequence numbers
    latest = receiver.latest();
    check(latest.session == 2 && near(latest.frame[mvr::TrackerRole::Hip].position.y, 2.0f, 1e-6f),
          "receiver: accepts new session");
    receiver.stop();
}

bool sameRotation(const mvr::Quat& a, const mvr::Quat& b, float degrees)
{
    return mvr::Quat::angleBetween(a, b) <= degrees * mvr::kDegToRad;
}

void testQuaternionBasis()
{
    // fromBasis must agree with rotate() for arbitrary rotations.
    const mvr::Quat q = mvr::Quat::fromAxisAngle(mvr::Vec3{0.3f, -0.8f, 0.5f}.normalized(), 2.1f);
    const mvr::Quat r = mvr::Quat::fromBasis(q.rotate({1, 0, 0}), q.rotate({0, 1, 0}), q.rotate({0, 0, 1}));
    check(sameRotation(q, r, 0.1f), "math: fromBasis inverts rotate");
}

void testHandCalibration()
{
    using mvr::TrackerRole;
    // Left arm in a T-pose: pointing along -X, palm down.
    mvr::TrackingFrame tpose;
    tpose[TrackerRole::LeftElbow] = {true, {-0.45f, 1.45f, 0}, {}};
    const mvr::Quat wristRest = mvr::Quat::fromAxisAngle(mvr::Vec3{1, 2, 3}.normalized(), 0.7f); // arbitrary joint axes
    tpose[TrackerRole::LeftHand] = {true, {-0.7f, 1.45f, 0}, wristRest};

    mvr::Quat expected;
    check(mvr::forearmOrientation(tpose[TrackerRole::LeftElbow].position, tpose[TrackerRole::LeftHand].position,
                                  expected),
          "hands: forearm orientation");
    check(mvr::Vec3(expected.rotate({0, 0, -1})).dot({-1, 0, 0}) > 0.999f &&
              expected.rotate({0, 1, 0}).dot({0, 1, 0}) > 0.999f,
          "hands: controller points along the arm, back of hand up");

    mvr::HandCalibration calib;
    const mvr::TrackerPose before = calib.controllerPose(tpose, mvr::Hand::Left);
    check(before.valid && sameRotation(before.orientation, expected, 0.1f), "hands: uncalibrated uses forearm");
    check(!calib.calibrate(tpose), "hands: calibration reports missing right arm");
    check(calib.calibrated(mvr::Hand::Left), "hands: left calibrated");

    // Rotate the whole arm (including a wrist roll the forearm can't show);
    // the calibrated controller must follow the wrist exactly.
    const mvr::Quat move = mvr::Quat::fromAxisAngle(mvr::Vec3{0.2f, 1, -0.4f}.normalized(), 1.3f);
    mvr::TrackingFrame moved = tpose;
    moved[TrackerRole::LeftHand].orientation = move * wristRest;
    const mvr::TrackerPose after = calib.controllerPose(moved, mvr::Hand::Left);
    check(after.valid && sameRotation(after.orientation, move * expected, 0.1f), "hands: calibrated follows wrist");
    const mvr::Vec3 palm = after.position - moved[TrackerRole::LeftHand].position;
    check(near(palm.length(), mvr::HandCalibration::kPalmOffset, 1e-4f), "hands: palm offset");
}

namespace D = O3DS::Data;

// Builds an Open3DStream packet with one subject (performer or rigid body).
struct O3dsNode {
    int parent;
    const char* name;
    mvr::Vec3 t;
    mvr::Quat r;
};

std::vector<uint8_t> o3dsDefinition(bool performer, const char* name, const char* uuid,
                                    const std::vector<O3dsNode>& nodes, D::Direction x, D::Direction y,
                                    D::Direction z, D::DistanceUnit unit)
{
    flatbuffers::FlatBufferBuilder b;
    std::vector<flatbuffers::Offset<D::Transform>> transforms;
    for (const O3dsNode& n : nodes) {
        const D::Translation t(n.t.x, n.t.y, n.t.z);
        const D::Rotation r(n.r.x, n.r.y, n.r.z, n.r.w);
        const D::Scale s(1, 1, 1);
        const std::vector<int8_t> order = {D::Component_Translation, D::Component_Rotation, D::Component_Scale};
        transforms.push_back(D::CreateTransformDirect(b, n.parent, n.name, &t, &r, &s, nullptr, &order));
    }
    auto data = D::CreateSubjectDataDirect(b, &transforms, name, uuid);
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<D::Performer>>> performers;
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<D::Rigidbody>>> rigidbodies;
    if (performer)
        performers = b.CreateVector(std::vector{D::CreatePerformer(b, data)});
    else
        rigidbodies = b.CreateVector(std::vector{D::CreateRigidbody(b, data)});
    D::SubjectListBuilder list(b);
    if (performer)
        list.add_performers(performers);
    else
        list.add_rigidbodies(rigidbodies);
    list.add_time(1.5);
    list.add_x_axis(x);
    list.add_y_axis(y);
    list.add_z_axis(z);
    list.add_distance_unit(unit);
    b.Finish(list.Finish());
    return {b.GetBufferPointer(), b.GetBufferPointer() + b.GetSize()};
}

std::vector<uint8_t> o3dsRotationUpdate(const char* uuid, int node, mvr::Quat r)
{
    flatbuffers::FlatBufferBuilder b;
    const std::vector<D::RotationUpdate> rotations = {D::RotationUpdate(r.x, r.y, r.z, r.w, node)};
    auto update = D::CreateSubjectUpdateDirect(b, nullptr, &rotations, nullptr, uuid);
    auto v = b.CreateVector(std::vector{D::CreatePerformerUpdate(b, update)});
    D::SubjectListBuilder list(b);
    list.add_performer_updates(v);
    b.Finish(list.Finish());
    return {b.GetBufferPointer(), b.GetBufferPointer() + b.GetSize()};
}

bool nearVec(const mvr::Vec3& a, const mvr::Vec3& b, float eps = 1e-4f)
{
    return (a - b).length() <= eps;
}

void testO3dsSkeleton()
{
    using mvr::TrackerRole;
    // Y-up, right-handed, centimeters, with MotionBuilder-style joint names.
    const auto def = o3dsDefinition(true, "Actor1", "uuid-1",
                                    {{-1, "Hips", {0, 100, 0}, {}},
                                     {0, "LeftLeg", {-10, -50, 0}, {}},
                                     {1, "LeftFoot", {0, -45, 0}, {}}},
                                    D::Direction_Right, D::Direction_Up, D::Direction_Back,
                                    D::DistanceUnit_Centimeter);
    mvr::o3ds::Decoder decoder({});
    mvr::TrackingFrame frame;
    std::string error;
    check(decoder.decode(def.data(), def.size(), frame, error), "o3ds: decodes definition");
    check(frame[TrackerRole::Hip].valid && nearVec(frame[TrackerRole::Hip].position, {0, 1, 0}), "o3ds: hip position");
    check(nearVec(frame[TrackerRole::LeftKnee].position, {-0.1f, 0.5f, 0}), "o3ds: knee follows hierarchy");
    check(nearVec(frame[TrackerRole::LeftFoot].position, {-0.1f, 0.05f, 0}), "o3ds: foot follows hierarchy");
    check(frame.timestampUs == 1500000, "o3ds: timestamp");
    check(decoder.summary() == "performer Actor1 · 3/11 points mapped", "o3ds: summary");

    // Turn the hips 90 degrees about +Y; the knee swings round with them.
    const auto upd = o3dsRotationUpdate("uuid-1", 0, mvr::Quat::fromAxisAngle({0, 1, 0}, 90 * mvr::kDegToRad));
    check(decoder.decode(upd.data(), upd.size(), frame, error), "o3ds: decodes update");
    check(nearVec(frame[TrackerRole::LeftKnee].position, {0, 0.5f, 0.1f}), "o3ds: update applied to hierarchy");
}

void testO3dsLeftHandedRigidBody()
{
    using mvr::TrackerRole;
    // Unreal-style: +X forward, +Y right, +Z up (left-handed), meters.
    const mvr::Quat yaw = mvr::Quat::fromAxisAngle({0, 0, 1}, 90 * mvr::kDegToRad); // about sender up
    const auto def = o3dsDefinition(false, "Waist", "rb-1", {{-1, "Waist", {2, 1, 1}, yaw}}, D::Direction_Forward,
                                    D::Direction_Right, D::Direction_Up, D::DistanceUnit_Meter);
    mvr::o3ds::Decoder decoder({});
    mvr::TrackingFrame frame;
    std::string error;
    check(decoder.decode(def.data(), def.size(), frame, error), "o3ds: decodes rigid body");
    const mvr::TrackerPose& hip = frame[TrackerRole::Hip];
    // forward 2, right 1, up 1 -> canonical (+X right, +Y up, -Z forward)
    check(hip.valid && nearVec(hip.position, {1, 1, -2}), "o3ds: left-handed position converted");
    // A turn that maps sender forward to sender right must do the same in canonical space.
    check(nearVec(hip.orientation.rotate({0, 0, -1}), {1, 0, 0}), "o3ds: left-handed rotation converted");

    const uint8_t garbage[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    check(!decoder.decode(garbage, sizeof(garbage), frame, error), "o3ds: rejects garbage");
}

void testO3dsUdpReassembly()
{
    std::vector<uint8_t> payload(100);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<uint8_t>(i);
    auto fragment = [&](uint32_t frameId, uint32_t index) {
        const uint32_t fragSize = 40, total = static_cast<uint32_t>(payload.size());
        std::vector<uint8_t> d;
        for (uint32_t v : {frameId, index, total, fragSize})
            for (int s = 0; s < 32; s += 8)
                d.push_back(static_cast<uint8_t>(v >> s));
        const size_t begin = index * fragSize, end = std::min<size_t>(begin + fragSize, total);
        d.insert(d.end(), payload.begin() + begin, payload.begin() + end);
        return d;
    };

    mvr::o3ds::UdpReassembler r;
    std::vector<uint8_t> out;
    auto f2 = fragment(7, 2), f0 = fragment(7, 0), f1 = fragment(7, 1);
    check(!r.add(f2.data(), f2.size(), out) && !r.add(f0.data(), f0.size(), out) && !r.add(f0.data(), f0.size(), out),
          "o3ds udp: waits for all fragments");
    check(r.add(f1.data(), f1.size(), out) && out == payload, "o3ds udp: reassembles out-of-order fragments");
    auto late = fragment(6, 0);
    check(!r.add(late.data(), 10, out), "o3ds udp: rejects truncated fragment");
}

void testJointNames()
{
    using mvr::TrackerRole;
    check(mvr::normalizeJointName("mixamorig:Left_Fore Arm") == "leftforearm", "names: strips namespace and separators");
    const std::vector<std::string> unreal = {"root", "pelvis", "spine_03", "lowerarm_l", "calf_r", "foot_l"};
    std::vector<std::string> normalized;
    for (const auto& n : unreal)
        normalized.push_back(mvr::normalizeJointName(n));
    check(mvr::findRole(TrackerRole::Hip, normalized) == 1 && mvr::findRole(TrackerRole::Chest, normalized) == 2 &&
              mvr::findRole(TrackerRole::LeftElbow, normalized) == 3 &&
              mvr::findRole(TrackerRole::RightKnee, normalized) == 4 && mvr::findRole(TrackerRole::Head, normalized) == -1,
          "names: Unreal skeleton");
}

// A finger along +X from a wrist at the origin, bending by the given angles
// (degrees) at each joint, in the XY plane.
mvr::FingerChain bentFinger(std::initializer_list<float> bends)
{
    mvr::FingerChain joints = {{0.08f, 0, 0}};
    float angle = 0;
    mvr::Vec3 p = joints[0];
    for (float b : bends) {
        angle += b * mvr::kDegToRad;
        p = p + mvr::Vec3{std::cos(angle), -std::sin(angle), 0} * 0.03f;
        joints.push_back(p);
    }
    return joints;
}

void testFingerCurl()
{
    using mvr::Finger;
    const mvr::Vec3 wrist{0, 0, 0};
    check(near(mvr::fingerCurl(Finger::Index, wrist, bentFinger({0, 0, 0})), 0, 1e-3f), "fingers: straight finger is 0");
    check(near(mvr::fingerCurl(Finger::Index, wrist, bentFinger({90, 100, 80})), 1, 1e-3f), "fingers: fist is 1");
    check(near(mvr::fingerCurl(Finger::Index, wrist, bentFinger({45, 50, 40})), 0.5f, 1e-3f), "fingers: half bent is 0.5");
    // Only two joints known: scaled by the first bend alone.
    check(near(mvr::fingerCurl(Finger::Index, wrist, bentFinger({45})), 0.5f, 1e-3f), "fingers: partial chain scaled");
    check(!mvr::fingerPose(wrist, {}).valid, "fingers: no chains, not valid");

    auto names = [](std::initializer_list<const char*> raw) {
        std::vector<std::string> out;
        for (const char* n : raw)
            out.push_back(mvr::normalizeJointName(n));
        return out;
    };
    const int index = static_cast<int>(Finger::Index), thumb = static_cast<int>(Finger::Thumb);
    auto humanIk = mvr::findFingerJoints(mvr::Hand::Left, names({"LeftHand", "LeftHandIndex1", "LeftHandIndex2",
                                                                  "LeftHandIndex3", "LeftHandIndex4"}));
    check((humanIk[index] == std::vector<int>{1, 2, 3, 4}), "fingers: HumanIK names");
    auto unreal = mvr::findFingerJoints(mvr::Hand::Right, names({"index_01_r", "index_02_r", "index_03_r", "index_01_l"}));
    check((unreal[index] == std::vector<int>{0, 1, 2}), "fingers: Unreal names, right hand only");
    auto xsens = mvr::findFingerJoints(mvr::Hand::Left, names({"LeftFirstMC", "LeftFirstPP", "LeftFirstDP",
                                                                "LeftSecondPP", "LeftSecondMP", "LeftSecondDP"}));
    check((xsens[thumb] == std::vector<int>{0, 1, 2}) && (xsens[index] == std::vector<int>{3, 4, 5}), "fingers: Xsens names");
    auto unity = mvr::findFingerJoints(mvr::Hand::Left, names({"Left Little Proximal", "Left Little Intermediate",
                                                                "Left Little Distal"}));
    check((unity[static_cast<int>(Finger::Pinky)] == std::vector<int>{0, 1, 2}), "fingers: Unity names");
}

void testFingerProtocol()
{
    mvr::TrackingFrame in;
    in.fingers[1].valid = true;
    in.fingers[1].curl = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    in.fingers[1].splay[2] = -0.5f;
    unsigned char buf[mvr::stream::kMaxPacketSize];
    const size_t size = mvr::stream::encode(in, 1, 1, mvr::stream::kFlagFingers, buf);
    mvr::stream::PacketHeader header;
    mvr::TrackingFrame out;
    check(mvr::stream::decode(buf, size, header, out) && header.fingerCount == 1, "protocol: finger entry");
    check(!out.fingers[0].valid && out.fingers[1].valid && near(out.fingers[1].curl[4], 0.5f, 1e-6f) &&
              near(out.fingers[1].splay[2], -0.5f, 1e-6f),
          "protocol: finger values");
    const size_t without = mvr::stream::encode(in, 1, 2, 0, buf);
    check(mvr::stream::decode(buf, without, header, out) && header.fingerCount == 0 && !out.fingers[1].valid,
          "protocol: fingers omitted without flag");
}

void testO3dsFingers()
{
    // Left hand at the origin pointing +X, index finger curled 45/50/40 degrees (half).
    const float d = 3.0f; // cm per segment
    const float a1 = 45 * mvr::kDegToRad, a2 = 95 * mvr::kDegToRad, a3 = 135 * mvr::kDegToRad;
    auto seg = [&](float a) { return mvr::Vec3{d * std::cos(a), -d * std::sin(a), 0}; };
    // Local translations (parent-relative, no rotations) along the bent chain.
    const auto def = o3dsDefinition(true, "Hand", "hand-1",
                                    {{-1, "Hips", {0, 0, 0}, {}},
                                     {0, "LeftHand", {0, 0, 0}, {}},
                                     {1, "LeftHandIndex1", {8, 0, 0}, {}},
                                     {2, "LeftHandIndex2", seg(a1), {}},
                                     {3, "LeftHandIndex3", seg(a2), {}},
                                     {4, "LeftHandIndex4", seg(a3), {}}},
                                    D::Direction_Right, D::Direction_Up, D::Direction_Back,
                                    D::DistanceUnit_Centimeter);
    mvr::o3ds::Decoder decoder({});
    mvr::TrackingFrame frame;
    std::string error;
    check(decoder.decode(def.data(), def.size(), frame, error) && frame.fingers[0].valid, "o3ds: finger data");
    check(near(frame.fingers[0].curl[static_cast<int>(mvr::Finger::Index)], 0.5f, 1e-3f), "o3ds: index curl");
    check(!frame.fingers[1].valid, "o3ds: no right-hand fingers");
}

} // namespace

int main()
{
    testProtocolRoundTrip();
    testAlignmentRecoversYawAndTranslation();
    testReceiverKeepsNewestPacket();
    testQuaternionBasis();
    testHandCalibration();
    testO3dsSkeleton();
    testO3dsLeftHandedRigidBody();
    testO3dsUdpReassembly();
    testJointNames();
    testFingerCurl();
    testFingerProtocol();
    testO3dsFingers();
    std::printf("%d failure(s)\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
