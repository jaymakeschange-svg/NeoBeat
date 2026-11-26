cmake_minimum_required (VERSION 3.22)
project (NeoBeat VERSION 3.0.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set_property (GLOBAL PROPERTY USE_FOLDERS ON)

# the sources are UTF-8 (comments use a few symbols); MSVC otherwise reads them in the local code page
if (MSVC)
    add_compile_options (/utf-8 /Z7)                # /Z7: debug info in the objects (no shared PDB while compiling in parallel)
    add_link_options (/DEBUG /OPT:REF /OPT:ICF)     # a PDB beside each binary, for readable crash stacks; the code is unchanged
endif()

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

set (NEOBEAT_UI_SOURCES
    Source/ui/Look.cpp
    Source/ui/Shell.cpp
    Source/ui/KitRoom.cpp
    Source/ui/Inspector.cpp
    Source/ui/MatrixRoom.cpp
    Source/ui/GridKeyMap.cpp
    Source/ui/Windows.cpp
    Source/ui/Panels.cpp)

target_sources (NeoBeat PRIVATE
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp
    ${NEOBEAT_UI_SOURCES})

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
# ---- the two effects on their own: one processor compiled twice (NB_FX_KIND 0 Friction Stretch, 1 Bounce Delay)
function (neobeat_add_fx target kind product code bundle)
    juce_add_plugin (${target}
        COMPANY_NAME "Marbelite"
        BUNDLE_ID "${bundle}"
        PLUGIN_MANUFACTURER_CODE Mrbl
        PLUGIN_CODE ${code}
        FORMATS VST3 Standalone
        PRODUCT_NAME "${product}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Delay)
    target_sources (${target} PRIVATE Source/fx/FxProcessor.cpp Source/fx/FxEditor.cpp)
    target_include_directories (${target} PRIVATE Source)
    target_compile_definitions (${target} PUBLIC
        NB_FX_KIND=${kind}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)
    target_link_libraries (${target}
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endfunction()
neobeat_add_fx (NeoBeatFriction 0 "Friction Stretch" Nbfs "com.marbelite.frictionstretch")
neobeat_add_fx (NeoBeatBounce 1 "Bounce Delay" Nbbd "com.marbelite.bouncedelay")

add_executable (neobeat_cli tools/neobeat_cli.cpp)
add_executable (test_fx tools/test_fx.cpp)
add_executable (test_sequencer tools/test_sequencer.cpp)
add_executable (test_workstation tools/test_workstation.cpp)
add_executable (test_livefx tools/test_livefx.cpp)
add_executable (test_room tools/test_room.cpp)
target_include_directories (test_room PRIVATE Source)
add_executable (test_analysis tools/test_analysis.cpp)
find_package (Threads REQUIRED)
target_link_libraries (test_workstation PRIVATE Threads::Threads)
# The workstation test is one long main() with dozens of project copies in it, and MSVC gives each block its
# own stack space: 8 MB for its main thread (the engine's own threads keep Windows' 1 MB, as in the plugin).
if (MSVC)
    target_link_options (test_workstation PRIVATE /STACK:8388608)
endif()
target_include_directories (test_fx PRIVATE Source)
target_link_libraries (test_fx PRIVATE Threads::Threads)

# Linux only: render the editor to a PNG (UI check without a display server).
if (UNIX AND NOT APPLE AND NEOBEAT_SNAPSHOT)
    juce_add_console_app (NeoBeatSnapshot PRODUCT_NAME "NeoBeatSnapshot")
    target_sources (NeoBeatSnapshot PRIVATE tools/Snapshot.cpp Source/plugin/PluginProcessor.cpp Source/plugin/PluginEditor.cpp ${NEOBEAT_UI_SOURCES})
    target_compile_definitions (NeoBeatSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries (NeoBeatSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    foreach (fx "Friction;0" "Bounce;1")
        list (GET fx 0 fxName)
        list (GET fx 1 fxKind)
        juce_add_console_app (Fx${fxName}HostTest PRODUCT_NAME "Fx${fxName}HostTest")
        target_sources (Fx${fxName}HostTest PRIVATE tools/FxHostTest.cpp Source/fx/FxProcessor.cpp Source/fx/FxEditor.cpp)
        target_include_directories (Fx${fxName}HostTest PRIVATE Source)
        target_compile_definitions (Fx${fxName}HostTest PRIVATE NB_FX_KIND=${fxKind} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries (Fx${fxName}HostTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endforeach()
    juce_add_console_app (NeoBeatHostTest PRODUCT_NAME "NeoBeatHostTest")
    target_sources (NeoBeatHostTest PRIVATE tools/HostTest.cpp Source/plugin/PluginProcessor.cpp Source/plugin/PluginEditor.cpp ${NEOBEAT_UI_SOURCES})
    target_compile_definitions (NeoBeatHostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries (NeoBeatHostTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
