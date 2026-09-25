cmake_minimum_required(VERSION 3.28)
project(MotionVRBridge VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MVR_BUILD_APP "Build the MotionVR Bridge desktop app (needs Slint)" ON)
option(MVR_BUILD_DRIVER "Build the SteamVR driver" ON)
option(MVR_BUILD_TESTS "Build unit tests" ON)

find_package(Threads REQUIRED)

# --- Shared code (app + driver) ----------------------------------------------
add_library(mvr_common STATIC
    src/core/HandCalibration.cpp
    src/net/UdpSocket.cpp
)
target_include_directories(mvr_common PUBLIC src)
set_target_properties(mvr_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (WIN32)
    target_link_libraries(mvr_common PUBLIC ws2_32)
endif()

# --- App ---------------------------------------------------------------------
if (MVR_BUILD_APP)
    # Uses an installed Slint if one is found, otherwise builds it from source
    # (requires a Rust toolchain; the first build takes a few minutes).
    find_package(Slint 1.18 QUIET)
    if (NOT Slint_FOUND)
        message(STATUS "Slint not found, fetching and building from source")
        include(FetchContent)
        # Features this app doesn't use.
        set(SLINT_FEATURE_INTERPRETER OFF CACHE BOOL "" FORCE)
        set(SLINT_FEATURE_TESTING OFF CACHE BOOL "" FORCE)
        set(SLINT_FEATURE_SYSTEM_TRAY OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            Slint
            GIT_REPOSITORY https://github.com/slint-ui/slint.git
            GIT_TAG v1.18.1
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR api/cpp
            EXCLUDE_FROM_ALL # keep Slint's own install rules out of our packages
        )
        FetchContent_MakeAvailable(Slint)
        if (APPLE)
            # Slint only does this when it is the top-level project: give the
            # dylib an @rpath install name so it can be bundled with the app.
            corrosion_add_target_local_rustflags(slint_cpp -Clink-arg=-Wl,-install_name,@rpath/libslint_cpp.dylib)
            set_target_properties(slint_cpp-shared PROPERTIES IMPORTED_NO_SONAME 0 IMPORTED_SONAME libslint_cpp.dylib)
        endif()
    endif()

    # RemoteCaptury is compiled directly rather than via its own CMakeLists,
    # which also builds the Python bindings and examples.
    set(REMOTECAPTURY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/RemoteCaptury)
    if (NOT EXISTS ${REMOTECAPTURY_DIR}/RemoteCaptury.cpp)
        message(FATAL_ERROR "RemoteCaptury missing. Run: git submodule update --init")
    endif()
    add_library(RemoteCaptury STATIC ${REMOTECAPTURY_DIR}/RemoteCaptury.cpp)
    target_include_directories(RemoteCaptury PUBLIC ${REMOTECAPTURY_DIR})
    target_link_libraries(RemoteCaptury PUBLIC Threads::Threads)
    if (WIN32)
        target_link_libraries(RemoteCaptury PUBLIC ws2_32 iphlpapi)
    endif()

    add_executable(MotionVRBridge MACOSX_BUNDLE WIN32
        src/main.cpp
        src/core/Bridge.cpp
        src/core/Registry.cpp
        src/net/OscSender.cpp
        src/sources/CapturySource.cpp
        src/sources/TestPatternSource.cpp
        src/sinks/SteamVRSink.cpp
        src/sinks/VRChatOscSink.cpp
    )
    target_link_libraries(MotionVRBridge PRIVATE mvr_common Slint::Slint RemoteCaptury Threads::Threads)
    slint_target_sources(MotionVRBridge ui/app.slint NAMESPACE ui)
    # App icon (regenerate with packaging/icon/make_icon.py).
    if (APPLE)
        set(MACOS_ICON ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/MotionVRBridge.icns)
        target_sources(MotionVRBridge PRIVATE ${MACOS_ICON})
        set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    elseif (WIN32)
        target_sources(MotionVRBridge PRIVATE packaging/windows/MotionVRBridge.rc)
    endif()
    set_target_properties(MotionVRBridge PROPERTIES
        MACOSX_BUNDLE_ICON_FILE MotionVRBridge.icns
        MACOSX_BUNDLE_BUNDLE_NAME "MotionVR Bridge"
        MACOSX_BUNDLE_GUI_IDENTIFIER com.medcelerate.motionvrbridge
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    if (MSVC)
        # WIN32 hides the console; keep the standard main() entry point.
        target_link_options(MotionVRBridge PRIVATE /ENTRY:mainCRTStartup)
    endif()

    # Vicon DataStream SDK is proprietary and not redistributed here. Point
    # VICON_SDK_DIR at the folder with DataStreamClient.h and the CPP library
    # (e.g. the extracted Mac/ or Linux64/ folder, or the Win64 install).
    set(VICON_SDK_DIR "" CACHE PATH "Vicon DataStream SDK folder (optional)")
    find_path(VICON_INCLUDE_DIR DataStreamClient.h
        HINTS ${VICON_SDK_DIR}
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ViconDataStreamSDK/Mac
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ViconDataStreamSDK/Linux64
            "C:/Program Files/Vicon/DataStream SDK/Win64/CPP"
        NO_DEFAULT_PATH)
    find_library(VICON_LIBRARY ViconDataStreamSDK_CPP HINTS ${VICON_INCLUDE_DIR} NO_DEFAULT_PATH)
    if (VICON_INCLUDE_DIR AND VICON_LIBRARY)
        message(STATUS "Vicon DataStream SDK: ${VICON_INCLUDE_DIR}")
        target_sources(MotionVRBridge PRIVATE src/sources/ViconSource.cpp)
        target_include_directories(MotionVRBridge PRIVATE ${VICON_INCLUDE_DIR})
        target_link_libraries(MotionVRBridge PRIVATE ${VICON_LIBRARY})
        target_compile_definitions(MotionVRBridge PRIVATE MVR_HAVE_VICON)
        # The SDK library is loaded from next to the executable.
        if (WIN32)
            file(GLOB VICON_RUNTIME "${VICON_INCLUDE_DIR}/*.dll")
        elseif (APPLE)
            file(GLOB VICON_RUNTIME "${VICON_INCLUDE_DIR}/libViconDataStreamSDK_CPP*.dylib")
        else()
            file(GLOB VICON_RUNTIME "${VICON_INCLUDE_DIR}/libViconDataStreamSDK_CPP*.so*")
            set_property(TARGET MotionVRBridge APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        endif()
        add_custom_command(TARGET MotionVRBridge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VICON_RUNTIME} $<TARGET_FILE_DIR:MotionVRBridge>)
    else()
        message(STATUS "Vicon DataStream SDK not found; building without the Vicon source (set VICON_SDK_DIR)")
    endif()

    # Packaging layout: the app at the root (a .app bundle on macOS) with the
    # Slint runtime library beside it.
    install(TARGETS MotionVRBridge RUNTIME DESTINATION . BUNDLE DESTINATION .)
    if (UNIX)
        # Run from the build tree without installing.
        set_property(TARGET MotionVRBridge APPEND PROPERTY BUILD_RPATH "$<TARGET_FILE_DIR:slint_cpp-shared>")
    endif()
    if (APPLE)
        set_target_properties(MotionVRBridge PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
        install(FILES $<TARGET_FILE:slint_cpp-shared> DESTINATION MotionVRBridge.app/Contents/Frameworks)
    elseif (UNIX)
        set_target_properties(MotionVRBridge PROPERTIES INSTALL_RPATH "$ORIGIN")
        install(FILES $<TARGET_FILE:slint_cpp-shared> packaging/linux/motionvrbridge.png DESTINATION .)
    else()
        install(FILES $<TARGET_FILE:slint_cpp-shared> DESTINATION .)
        # Slint.dll must also sit next to the executable in the build tree.
        add_custom_command(TARGET MotionVRBridge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:MotionVRBridge> $<TARGET_FILE_DIR:MotionVRBridge>
            COMMAND_EXPAND_LISTS)
    endif()
    if (VICON_RUNTIME AND NOT APPLE)
        install(FILES ${VICON_RUNTIME} DESTINATION .)
    endif()
endif()

# --- SteamVR driver ----------------------------------------------------------
if (MVR_BUILD_DRIVER OR MVR_BUILD_TESTS)
    add_library(mvr_driver_core STATIC
        driver/src/PlayspaceAlignment.cpp
        driver/src/StreamReceiver.cpp
    )
    target_include_directories(mvr_driver_core PUBLIC driver/src)
    target_link_libraries(mvr_driver_core PUBLIC mvr_common Threads::Threads)
    set_target_properties(mvr_driver_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if (MVR_BUILD_DRIVER)
    # Only the driver header of the OpenVR SDK is needed.
    set(OPENVR_VERSION v2.15.6)
    set(OPENVR_INCLUDE_DIR ${CMAKE_BINARY_DIR}/openvr/${OPENVR_VERSION})
    if (NOT EXISTS ${OPENVR_INCLUDE_DIR}/openvr_driver.h)
        file(DOWNLOAD
            https://raw.githubusercontent.com/ValveSoftware/openvr/${OPENVR_VERSION}/headers/openvr_driver.h
            ${OPENVR_INCLUDE_DIR}/openvr_driver.h
            EXPECTED_HASH SHA256=1036efe998d63e82d1d3db2b32a2f58df4a8eeaf5280f50aaf28220ff60a40ab
            STATUS OPENVR_DOWNLOAD_STATUS)
        list(GET OPENVR_DOWNLOAD_STATUS 0 OPENVR_DOWNLOAD_CODE)
        if (NOT OPENVR_DOWNLOAD_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download openvr_driver.h: ${OPENVR_DOWNLOAD_STATUS}")
        endif()
    endif()

    if (WIN32)
        set(STEAMVR_PLATFORM win64)
    elseif (APPLE)
        set(STEAMVR_PLATFORM osx64) # SteamVR doesn't support macOS; builds for compile checks only
    else()
        set(STEAMVR_PLATFORM linux64)
    endif()

    # SteamVR expects: motionvrbridge/driver.vrdrivermanifest,
    # motionvrbridge/resources/..., motionvrbridge/bin/<platform>/driver_motionvrbridge.<ext>
    set(DRIVER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/steamvr/motionvrbridge)
    add_library(driver_motionvrbridge SHARED
        driver/src/DriverMain.cpp
        driver/src/HandController.cpp
        driver/src/TrackerDevice.cpp
    )
    target_include_directories(driver_motionvrbridge PRIVATE ${OPENVR_INCLUDE_DIR})
    target_link_libraries(driver_motionvrbridge PRIVATE mvr_driver_core)
    set_target_properties(driver_motionvrbridge PROPERTIES
        PREFIX ""
        CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY ${DRIVER_OUTPUT_DIR}/bin/${STEAMVR_PLATFORM}
        RUNTIME_OUTPUT_DIRECTORY ${DRIVER_OUTPUT_DIR}/bin/${STEAMVR_PLATFORM}
    )
    foreach (config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} CONFIG)
        set_target_properties(driver_motionvrbridge PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${CONFIG} ${DRIVER_OUTPUT_DIR}/bin/${STEAMVR_PLATFORM}
            RUNTIME_OUTPUT_DIRECTORY_${CONFIG} ${DRIVER_OUTPUT_DIR}/bin/${STEAMVR_PLATFORM})
    endforeach()
    add_custom_command(TARGET driver_motionvrbridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/driver/motionvrbridge ${DRIVER_OUTPUT_DIR})

    # Packaging layout: driver/motionvrbridge/ plus a script that registers it
    # with SteamVR.
    if (NOT APPLE)
        install(DIRECTORY driver/motionvrbridge/ DESTINATION driver/motionvrbridge)
        install(TARGETS driver_motionvrbridge
            RUNTIME DESTINATION driver/motionvrbridge/bin/${STEAMVR_PLATFORM}
            LIBRARY DESTINATION driver/motionvrbridge/bin/${STEAMVR_PLATFORM})
        if (WIN32)
            install(FILES packaging/windows/register-driver.ps1 DESTINATION .)
        else()
            install(PROGRAMS packaging/linux/register-driver.sh DESTINATION .)
        endif()
    endif()
endif()

# --- License files in every package -------------------------------------------
if (APPLE AND MVR_BUILD_APP)
    install(FILES LICENSE LICENSE-EXCEPTION THIRD_PARTY_NOTICES.md DESTINATION MotionVRBridge.app/Contents/Resources)
elseif (MVR_BUILD_APP OR MVR_BUILD_DRIVER)
    install(FILES LICENSE LICENSE-EXCEPTION THIRD_PARTY_NOTICES.md DESTINATION .)
endif()

# --- Tests -------------------------------------------------------------------
if (MVR_BUILD_TESTS)
    enable_testing()
    add_executable(mvr_tests tests/tests.cpp)
    target_link_libraries(mvr_tests PRIVATE mvr_driver_core)
    add_test(NAME mvr_tests COMMAND mvr_tests)
endif()
