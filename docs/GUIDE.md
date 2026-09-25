# MotionVR Bridge guide

Setup details, the SteamVR driver, and building from source.

## First launch

Release builds aren't code-signed yet:

- **Windows:** if SmartScreen appears, choose **More info → Run anyway**.
- **macOS:** right-click `MotionVRBridge.app` and choose **Open**, or run
  `xattr -dr com.apple.quarantine MotionVRBridge.app`.
- **Linux:** needs glibc 2.38+ (Ubuntu 24.04+, Fedora 39+), fontconfig and xkbcommon.

## Build

Requires CMake ≥ 3.28, a C++20 compiler, and a Rust toolchain. Slint is built from
source (as a static library) on the first build unless an installed Slint is found.

```bash
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/MotionVRBridge
```

## Sources

| Source | Notes |
|---|---|
| Captury Live | Uses [RemoteCaptury](https://github.com/thecaptury/RemoteCaptury). Connects asynchronously and reconnects on its own. Leave the host empty to auto-discover over multicast. Follows the first actor that streams unless you set an Actor ID. |
| Vicon DataStream | Optional, needs the Vicon DataStream SDK (see below). Connects to Shogun, Nexus, Tracker or Evoke (default `localhost:801`). Maps segments by name: Shogun skeletons (`Hips`, `LeftForeArm`, `LeftLeg`…), Plug-in Gait (`Pelvis`, `LRadius`, `LTibia`…), or rigid-body objects named after body parts (`Waist`, `LeftFoot`, `Head`…). Named rigid bodies win over skeleton segments. |
| Xsens MVN | Receives MVN Animate / Analyze's Network Streamer on UDP (default port 9763). In MVN, add a network streaming target with this PC's IP and port and set the pose data to **Position + Quaternion**. Includes finger segments when gloves are used. Follows the first character streamed unless you enter a character number. |
| Open3DStream | Receives [Open3DStream](https://www.open3dstream.com/) from MotionBuilder, Maya, Unreal or any other sender. Pick the protocol that pairs with the sender's (Publish → NNG Subscribe, Pair Server ↔ Pair Client, Pipeline Push → NNG Pipeline Pull, UDP → UDP) and its address (default `tcp://127.0.0.1:6001`; for UDP, `udp://0.0.0.0:<port>` to listen). Axes and units come from the stream; the fallbacks apply only to senders that don't send them. Joints are matched by name (HumanIK, Mixamo, Unreal and Shogun names), and single-node subjects named after a body part (`Waist`, `LeftFoot`…) count as rigid bodies. |
| Recording | Plays a recorded take back into any target (see [Recording](#recording)). |
| Test Pattern | A synthetic body that marches and waves, for testing targets without a capture rig. |

### Vicon DataStream SDK

The Vicon DataStream SDK 1.13 client library is included in `third_party/ViconDataStreamSDK`
(redistributed with permission; see its README), so every build has the Vicon source. To use a
different SDK version, pass `-DVICON_SDK_DIR=<folder containing DataStreamClient.h and the CPP library>`.

The Linux library needs glibc 2.38 or newer (Ubuntu 24.04+, Fedora 39+).

## Recording

Press **Record** while streaming to save everything the source sends: body points, finger curls
and controller input. Takes go to `Documents/MotionVR Bridge/Recordings` (choose another
folder in the recording settings, under the cog next to Record), named after the date and time unless a take name is given.

To play a take back, pick the **Recording** source and the take. **Speed** plays slower or faster.
With **Interpolate** on (the default), frames are generated at a steady **Output rate** by
blending between recorded frames, so slow motion stays smooth instead of repeating frames;
turn it off to replay the exact recorded frames.

### OSC control

Turn on **OSC control** in the recording settings (the cog) to start and stop recording from any OSC tool
(TouchDesigner, QLab, a DAW…). Send to this machine on the OSC port (default 9100):

| Address | Arguments | |
|---|---|---|
| `/mvb/record` | `1` or `0`, optional take name | start / stop |
| `/mvb/record/start` | optional take name | start |
| `/mvb/record/stop` | | stop |
| `/mvb/record/toggle` | | start or stop |

### Synchronized recording

Turn on **Synchronized recording** on each machine. Instances find each other on the local
network with DNS-SD (Bonjour, service `_mvb._udp`), and the recording settings list the ones found.
Starting or stopping on any of them (button or OSC) does the same on all of them, under the same
take name with each machine's name appended, e.g. `Scene 1 - Studio A.mvb`. Every file stores its
start time (`start_unix_ms`) for lining takes up afterwards. The OSC port must be reachable
between the machines.

### File format

`.mvb` files are a stream of [MessagePack](https://msgpack.org) objects, so any MessagePack
library can read them. The first object is a header describing the rest (point names, value order,
coordinates: right-handed, +Y up, -Z forward, meters); each following object is one frame,
`[time_us, points, fingers, controllers]`, with `nil` for points that weren't tracked.

```python
import msgpack

with open("Scene 1.mvb", "rb") as f:
    header, *frames = msgpack.Unpacker(f)
hip = header["points"].index("Hip")
for time_us, points, fingers, controllers in frames:
    if points[hip]:
        x, y, z, qx, qy, qz, qw = points[hip]
```

**Also save CSV** writes a spreadsheet-friendly copy next to each take (one row per frame).

## Targets

| Target | Notes |
|---|---|
| VRChat OSC Trackers | Implements the [OSC Trackers spec](https://docs.vrchat.com/docs/osc-trackers). Sends hip, chest, feet, knees, and elbows to `/tracking/trackers/1-8`, and optionally the head for alignment. Enable OSC in VRChat, then calibrate full body as usual. |
| SteamVR Trackers | Streams to the bundled SteamVR driver (below), which shows up as Vive-style trackers in every SteamVR/OpenXR app. Optionally publishes the hands as controllers. |

Click an indicator on the body to stop forwarding that point. Hollow indicators are
points the selected target can't use.

## SteamVR driver

`driver_motionvrbridge` publishes hip, chest, elbows, knees, and feet as virtual Vive trackers.
It builds to `build/steamvr/motionvrbridge/` on Windows or Linux (SteamVR has no macOS support).

The Windows installers register it automatically. Otherwise, start SteamVR once, then run the
script shipped next to the app (`register-driver.ps1` on Windows, `register-driver.sh` on Linux),
or register a development build by hand, then restart SteamVR:

```bash
"<Steam>/steamapps/common/SteamVR/bin/win64/vrpathreg.exe" adddriver "<repo>/build/steamvr/motionvrbridge"
```

Install either the full app or the driver-only package on a machine, not both: SteamVR would
see two copies of the same driver.

Then pick **SteamVR Trackers** as the target in the app and press Start.

**Playspace alignment.** The capture system's world doesn't match SteamVR's playspace, so the
driver lines them up by comparing the capture system's head joint with the headset. Position
aligns right away. Rotation locks once you've walked around about half a metre, and it keeps
refining as you move. Each Start begins a fresh alignment. Keep the head indicator enabled.

Settings live in `steamvr.vrsettings` under `driver_motionvrbridge`:

| Key | Default | |
|---|---|---|
| `port` | `39570` | UDP port to listen on |
| `listen_address` | `127.0.0.1` | Use `0.0.0.0` if the app runs on another machine |
| `align_to_hmd` | `true` | Turn off if the capture system is already calibrated to SteamVR space |

Tracker roles come from the controller type (`vive_tracker_waist`, and so on). If SteamVR
doesn't assign them itself, set them under **Settings → Controllers → Manage Trackers**.

### Hands as controllers

Turn on **Publish hands as controllers** on the SteamVR target to drive your in-game hands from
the capture system. The hands appear as Valve Index controllers, so games use their existing
Index bindings. Only use this with no real controllers connected, since both would claim the
hand roles.

Before calibration, the controller orientation follows the forearm and assumes palms down.
Stand in a T-pose with palms down and press **Calibrate hands** so wrist rotation is followed
exactly. Buttons, trigger and grip are wired through the protocol but nothing drives them yet;
gesture mapping is planned.

### Finger tracking

With hands as controllers on, turn on **Finger tracking** to drive the controllers' hand skeletons
(SteamVR skeletal input, which VRChat and most games use for fingers) from the source's finger
data. The app measures how far each finger bends from its joint positions, so it works with any
naming: Captury finger bones, HumanIK/Mixamo (`LeftHandIndex1`…), Unreal (`index_01_l`…), Unity
(`Left Index Proximal`…) and Xsens (`LeftSecondPP`…). The target's status shows which hands have
finger data. Without finger data, the hand skeletons follow trigger and grip.

SteamVR keeps the controllers it first created until it restarts, so restart SteamVR after changing
this option.

## Architecture

- `src/core/Tracking.h`: the canonical `TrackingFrame` (right-handed, +Y up, −Z forward, meters).
- `src/core/Plugin.h`: the `TrackingSource`/`TrackingSink` interfaces and `ConfigField`s, which the UI renders automatically.
- `src/core/Registry.cpp`: the lists behind the two dropdowns. Register a new source or sink here.
- `src/core/Bridge.*`: forwards frames from the source thread to the sink and tracks per-point activity for the UI.
- `src/protocol/TrackerStream.h`: the UDP packet format between the app and the SteamVR driver.
- `driver/`: the SteamVR driver. `PlayspaceAlignment` and `StreamReceiver` have no OpenVR dependency and are covered by `tests/`.

Run the tests with `ctest --test-dir build`.

## Releases

`.github/workflows/build.yml` builds, tests and packages Windows, macOS and Linux on every
push. Pushing a `v*` tag publishes a GitHub Release with:

| Asset | Contents |
|---|---|
| `MotionVRBridge-Windows-Setup.exe` | App + SteamVR driver, registers the driver (NSIS, `packaging/windows/installer.nsi`) |
| `MotionVRBridge-Windows-Portable.zip` | App + driver + `register-driver.ps1` (includes the MSVC runtime) |
| `MotionVRBridge-SteamVR-Driver-Setup.exe` | Driver only, registers it |
| `MotionVRBridge-macOS.zip` | `MotionVRBridge.app` (Apple Silicon, unsigned) |
| `MotionVRBridge-Linux-x64.tar.gz` | App + driver + `register-driver.sh` (glibc 2.38+) |

Release builds include the Vicon source and its SDK library (see `LICENSE-EXCEPTION`).
