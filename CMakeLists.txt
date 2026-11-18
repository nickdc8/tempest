cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(Tempest VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is not committed. Use a local clone if there is one, otherwise share the
# checkout that the sibling Sighs project already has.
if(EXISTS "${CMAKE_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(JUCE_DIR_DEFAULT "${CMAKE_SOURCE_DIR}/JUCE")
else()
    set(JUCE_DIR_DEFAULT "${CMAKE_SOURCE_DIR}/../univibe/JUCE")
endif()
set(JUCE_DIR "${JUCE_DIR_DEFAULT}" CACHE PATH "Path to a JUCE 9 checkout")

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(Tempest
    COMPANY_NAME "Nick Cox"
    PLUGIN_MANUFACTURER_CODE Ncox
    PLUGIN_CODE Tmpt
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Tempest"
    BUNDLE_ID com.nickcox.tempest
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Dynamics Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Tempest PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Tempest PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Tempest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless check of the JUCE wrapper (oversampling, state, bus layouts).
juce_add_console_app(TempestProcessorTest PRODUCT_NAME "TempestProcessorTest")
target_sources(TempestProcessorTest PRIVATE
    tests/processor_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(TempestProcessorTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TempestProcessorTest PRIVATE
    juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
