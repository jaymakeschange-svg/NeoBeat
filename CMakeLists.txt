cmake_minimum_required (VERSION 3.22)
project (NeoBeat VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set_property (GLOBAL PROPERTY USE_FOLDERS ON)

# JUCE: a local checkout (-DJUCE_DIR=...) or fetched at configure time.
if (DEFINED JUCE_DIR)
    add_subdirectory (${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include (FetchContent)
    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable (JUCE)
endif()

juce_add_plugin (NeoBeat
    COMPANY_NAME "Marbelite"
    BUNDLE_ID "com.marbelite.neobeat"
    PLUGIN_MANUFACTURER_CODE Mrbl
    PLUGIN_CODE NeoB
    FORMATS VST3 Standalone
    PRODUCT_NAME "NeoBeat"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum)

target_sources (NeoBeat PRIVATE
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp)

target_compile_definitions (NeoBeat PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries (NeoBeat
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Framework-free tools: the offline renderer and the sequencer test run the
# exact engine the plugin uses.
add_executable (neobeat_cli tools/neobeat_cli.cpp)
add_executable (test_sequencer tools/test_sequencer.cpp)

# Linux only: render the editor to a PNG (UI check without a display server).
if (UNIX AND NOT APPLE AND NEOBEAT_SNAPSHOT)
    juce_add_console_app (NeoBeatSnapshot PRODUCT_NAME "NeoBeatSnapshot")
    target_sources (NeoBeatSnapshot PRIVATE tools/Snapshot.cpp Source/plugin/PluginProcessor.cpp Source/plugin/PluginEditor.cpp)
    target_compile_definitions (NeoBeatSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries (NeoBeatSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
