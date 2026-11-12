cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

project(Tourbillon VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(TOURBILLON_INSTALL_PLUGINS "Copy built plugins into ~/Library/Audio/Plug-Ins" ON)

# JUCE is shared with Atelier (the Channel project next door) rather than kept as a second copy.
# Point this somewhere else, or drop a JUCE folder in here, to build without the sibling project.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(TOURBILLON_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE")
else()
    set(TOURBILLON_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Channel/JUCE" CACHE PATH "Path to JUCE")
endif()
add_subdirectory("${TOURBILLON_JUCE_DIR}" JUCE)

juce_add_plugin(Tourbillon
    COMPANY_NAME "Channel Audio"
    BUNDLE_ID "audio.channel.tourbillon"
    PLUGIN_MANUFACTURER_CODE Chnl
    PLUGIN_CODE Tbln
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Tourbillon"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Modulation
    COPY_PLUGIN_AFTER_BUILD ${TOURBILLON_INSTALL_PLUGINS}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(Tourbillon PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(Tourbillon PUBLIC Source)

target_compile_definitions(Tourbillon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(Tourbillon
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline test harness: runs audio through the processor and checks the results.
juce_add_console_app(TourbillonTests PRODUCT_NAME "TourbillonTests")
target_sources(TourbillonTests PRIVATE Tests/Main.cpp)
target_link_libraries(TourbillonTests PRIVATE Tourbillon juce::juce_audio_utils juce::juce_dsp)
target_compile_options(TourbillonTests PRIVATE -Wno-macro-redefined)
