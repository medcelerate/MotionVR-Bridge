// Minimal self-checking tests for the pieces that don't need SteamVR or a UI.

#include "PlayspaceAlignment.h"
#include "core/HandCalibration.h"
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

} // namespace

int main()
{
    testProtocolRoundTrip();
    testAlignmentRecoversYawAndTranslation();
    testReceiverKeepsNewestPacket();
    testQuaternionBasis();
    testHandCalibration();
    std::printf("%d failure(s)\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
