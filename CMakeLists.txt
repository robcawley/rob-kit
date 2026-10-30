cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(ROB_KIT VERSION 1.2.0)
set(CMAKE_CXX_STANDARD 20)

# Reuse the JUCE checkout that NEON RIOT already has.
set(JUCE_DIR "${CMAKE_SOURCE_DIR}/../NEON RIOT/JUCE" CACHE PATH "Path to a JUCE 8 checkout")
add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_binary_data(RobKitFonts SOURCES
    Resources/Interface/kit_scene.png
    Resources/Interface/wordmark.png
    Resources/Interface/hit_halo.png
    Resources/Interface/panel_texture.png
    Resources/Interface/screw.png
    Resources/Interface/knob_strip.png
    Resources/Fonts/Rajdhani-SemiBold.ttf
    Resources/Fonts/Rajdhani-Bold.ttf
    Resources/Fonts/Rajdhani-Medium.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)

set(ROBKIT_SOURCES
    Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Parameters.cpp Source/DSP/Kit.cpp
    Source/UI/Theme.cpp Source/UI/Chassis.cpp Source/UI/Controls.cpp Source/UI/Tooltips.cpp)

juce_add_plugin(RobKit
    COMPANY_NAME "Rob Cawley"
    BUNDLE_ID "com.robcawley.robkit"
    PLUGIN_MANUFACTURER_CODE Rcwl
    PLUGIN_CODE Rkit
    PRODUCT_NAME "ROB KIT"
    DESCRIPTION "Human drum instrument"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    FORMATS AU Standalone
    COPY_PLUGIN_AFTER_BUILD FALSE
    VERSION 1.2.0)

target_sources(RobKit PRIVATE ${ROBKIT_SOURCES})
target_include_directories(RobKit PRIVATE Source)
target_compile_definitions(RobKit PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(RobKit PRIVATE RobKitFonts juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Put the samples inside the bundle, sign it, and install it.
set(ROBKIT_BUNDLE "${CMAKE_BINARY_DIR}/RobKit_artefacts/$<CONFIG>/AU/ROB KIT.component")
add_custom_command(TARGET RobKit_AU POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${ROBKIT_BUNDLE}/Contents/Resources/Samples"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ROBKIT_BUNDLE}/Contents/Resources/Samples"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Samples/Rob" "${ROBKIT_BUNDLE}/Contents/Resources/Samples/Rob"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/Samples/Fill"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Samples/Fill" "${ROBKIT_BUNDLE}/Contents/Resources/Samples/Fill"
    COMMAND codesign --force --deep --sign - "${ROBKIT_BUNDLE}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$ENV{HOME}/Library/Audio/Plug-Ins/Components/ROB KIT.component"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROBKIT_BUNDLE}" "$ENV{HOME}/Library/Audio/Plug-Ins/Components/ROB KIT.component"
    VERBATIM)

# Offline test renderer: grooves through the kit, repeat-similarity, real-time safety, UI snapshot.
juce_add_console_app(RobKitRender PRODUCT_NAME "RobKitRender")
target_sources(RobKitRender PRIVATE Tools/Render.cpp ${ROBKIT_SOURCES})
target_include_directories(RobKitRender PRIVATE Source)
target_compile_definitions(RobKitRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="ROB KIT"
    JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
target_link_libraries(RobKitRender PRIVATE RobKitFonts juce::juce_audio_utils juce::juce_audio_formats juce::juce_recommended_config_flags)
