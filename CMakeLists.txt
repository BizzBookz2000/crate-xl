cmake_minimum_required(VERSION 3.22)

# The app's name is set here (and nowhere else in the build).
set(CRATE_PRODUCT_NAME "Crate XL" CACHE STRING "Application name")

project(Crate VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

if(MSVC)
    add_compile_options(/MP /bigobj)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# JUCE (downloaded automatically, or point CRATE_JUCE_DIR at a local copy)
# ---------------------------------------------------------------------------
set(CRATE_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")
if(CRATE_JUCE_DIR)
    add_subdirectory(${CRATE_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(CRATE_ENGINE_SOURCES
    Source/Effects.cpp
    Source/Engine.cpp
    Source/EngineParams.cpp
    Source/EngineSequencer.cpp
    Source/EngineVoices.cpp
    Source/EngineMidi.cpp
    Source/EngineSamples.cpp
    Source/SessionIO.cpp
    Source/EngineImport.cpp
    Source/EngineResample.cpp
    Source/PlatformAudioWin.cpp
    Source/PluginManager.cpp)
if(APPLE)
    list(APPEND CRATE_ENGINE_SOURCES Source/PlatformAudioMac.mm)
endif()

set(CRATE_DEFINITIONS
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0)
if(APPLE)
    list(APPEND CRATE_DEFINITIONS JUCE_PLUGINHOST_AU=1)
else()
    list(APPEND CRATE_DEFINITIONS JUCE_PLUGINHOST_AU=0)
endif()

# ---------------------------------------------------------------------------
# The app
# ---------------------------------------------------------------------------
juce_add_gui_app(Crate
    PRODUCT_NAME "${CRATE_PRODUCT_NAME}"
    COMPANY_NAME "ILLAHTAINMENT"
    COMPANY_COPYRIGHT "Copyright (c) ILLAHTAINMENT. All rights reserved."
    BUNDLE_ID "com.illahtainment.cratexl"
    VERSION "${PROJECT_VERSION}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Records audio from your inputs onto pads and audio tracks."
    DOCUMENT_EXTENSIONS crsession)

juce_generate_juce_header(Crate)

juce_add_binary_data(CrateData SOURCES
    Resources/Fonts/Inter-Regular.ttf
    Resources/Fonts/Inter-SemiBold.ttf
    Resources/Fonts/Inter-Bold.ttf
    Resources/Logo.png)

target_sources(Crate PRIVATE
    ${CRATE_ENGINE_SOURCES}
    Source/Main.cpp
    Source/WindowStyle.cpp
    Source/UiCommon.cpp
    Source/MainComponent.cpp
    Source/PerformanceView.cpp
    Source/EditorView.cpp
    Source/SampleEditor.cpp
    Source/SequencerView.cpp
    Source/MixerView.cpp
    Source/ChannelEqView.cpp
    Source/Panels.cpp
    Source/Settings.cpp
    Source/Dialogs.cpp
    Source/StarterKit.cpp)

set(CRATE_USE_ASIO 0)
if(WIN32 AND CRATE_ASIO_SDK AND EXISTS "${CRATE_ASIO_SDK}/common/iasiodrv.h")
    set(CRATE_USE_ASIO 1)
    target_include_directories(Crate PRIVATE "${CRATE_ASIO_SDK}/common")
endif()

target_compile_definitions(Crate PRIVATE
    ${CRATE_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Crate,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Crate,JUCE_VERSION>"
    JUCE_ASIO=${CRATE_USE_ASIO})

target_link_libraries(Crate
    PRIVATE
        CrateData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(WIN32)
    target_link_libraries(Crate PRIVATE dwmapi)   # black title bars
    target_link_libraries(Crate PRIVATE mfplat mfreadwrite mfuuid ole32)   # Media Foundation decoding
endif()
if(APPLE)
    target_link_libraries(Crate PRIVATE "-framework AVFoundation" "-framework CoreMedia")
endif()

if(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(Crate PRIVATE OpenGL::GL)
endif()

# ---------------------------------------------------------------------------
# Optional headless engine tests:  cmake -DCRATE_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(CRATE_BUILD_TESTS "Build the headless engine test program" OFF)
if(CRATE_BUILD_TESTS)
    juce_add_console_app(CrateTests PRODUCT_NAME "CrateTests")
    juce_generate_juce_header(CrateTests)
    target_sources(CrateTests PRIVATE Tests/EngineTests.cpp ${CRATE_ENGINE_SOURCES})
    target_include_directories(CrateTests PRIVATE Source)
    target_compile_definitions(CrateTests PRIVATE ${CRATE_DEFINITIONS})
    if(WIN32)
        target_link_libraries(CrateTests PRIVATE mfplat mfreadwrite mfuuid ole32)
    elseif(APPLE)
        target_link_libraries(CrateTests PRIVATE "-framework AVFoundation" "-framework CoreMedia")
    endif()
    target_link_libraries(CrateTests PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
