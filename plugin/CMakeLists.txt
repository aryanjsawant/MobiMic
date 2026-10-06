cmake_minimum_required(VERSION 3.22)
project(MobiMic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static C runtime: users don't need the Visual C++ redistributable installed.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)

set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# No CMake project of its own; we compile the single C++ file directly.
FetchContent_Declare(qrcodegen
    GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
    GIT_TAG v1.8.0
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(JUCE mbedtls qrcodegen)

juce_add_plugin(MobiMic
    COMPANY_NAME "MobiMic"
    BUNDLE_ID com.mobimic.plugin
    PLUGIN_MANUFACTURER_CODE Mbmc
    PLUGIN_CODE Mbm1
    FORMATS VST3 Standalone
    PRODUCT_NAME "MobiMic"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Tools
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(MobiMicData SOURCES web/index.html)

target_sources(MobiMic PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PhoneServer.cpp
    src/NetInfo.cpp
    src/TakeWriter.cpp
    ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)

target_include_directories(MobiMic PRIVATE ${qrcodegen_SOURCE_DIR}/cpp)

target_compile_definitions(MobiMic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    NOMINMAX
    WIN32_LEAN_AND_MEAN)

target_link_libraries(MobiMic
    PRIVATE
        MobiMicData
        juce::juce_audio_utils
        MbedTLS::mbedtls
        MbedTLS::mbedx509
        MbedTLS::mbedcrypto
        ws2_32
        iphlpapi
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Headless integration test of the networking + buffering core (see tests/).
juce_add_console_app(MobiMicHeadlessTest PRODUCT_NAME "MobiMicHeadlessTest")
target_sources(MobiMicHeadlessTest PRIVATE
    tests/HeadlessTest.cpp
    src/PhoneServer.cpp
    src/TakeWriter.cpp)
target_compile_definitions(MobiMicHeadlessTest PRIVATE JUCE_USE_CURL=0 NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(MobiMicHeadlessTest PRIVATE
    MobiMicData
    juce::juce_core
    MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto
    ws2_32
    juce::juce_recommended_config_flags)

# Renders the plugin window to PNGs off-screen for the README (see tools/).
juce_add_console_app(MobiMicScreenshots PRODUCT_NAME "MobiMicScreenshots")
target_sources(MobiMicScreenshots PRIVATE
    tools/Screenshots.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PhoneServer.cpp
    src/NetInfo.cpp
    src/TakeWriter.cpp
    ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
target_include_directories(MobiMicScreenshots PRIVATE ${qrcodegen_SOURCE_DIR}/cpp)
target_compile_definitions(MobiMicScreenshots PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(MobiMicScreenshots PRIVATE
    MobiMicData
    juce::juce_audio_utils
    MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto
    ws2_32 iphlpapi
    juce::juce_recommended_config_flags)
