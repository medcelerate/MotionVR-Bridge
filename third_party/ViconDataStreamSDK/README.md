# Vicon DataStream SDK 1.13.0+167154h

Proprietary software from Vicon Motion Systems, redistributed with permission as part of
MotionVR Bridge. Only the C++ client library and its headers are included:

| Folder | Contents | Taken from |
|---|---|---|
| `include/` | `DataStreamClient.h`, `IDataStreamClientBase.h` (identical on all platforms) | SDK zip |
| `Mac/` | `libViconDataStreamSDK_CPP.dylib` (arm64) | `ViconDataStreamSDK_*_Mac.zip` |
| `Linux64/` | `libViconDataStreamSDK_CPP.so` (x86-64, needs glibc 2.38+) | `ViconDataStreamSDK_*_Linux64.zip` |
| `Win64/` | `ViconDataStreamSDK_CPP.dll` and `.lib` (x64, MSVC) | the `x64.msi`, extracted with `7z x` |

To update, download the new SDK from Vicon and replace these files with the same names.
