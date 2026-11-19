cmake_minimum_required (VERSION 3.22)
project (MidiJaQ VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set_property (GLOBAL PROPERTY USE_FOLDERS ON)

# the sources are UTF-8; MSVC otherwise reads them in the local code page
if (MSVC)
    add_compile_options (/utf-8 /Z7)                # /Z7: debug info in the objects (no shared PDB while compiling in parallel)
    add_link_options (/DEBUG /OPT:REF /OPT:ICF)     # a PDB beside each binary, for readable crash stacks
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

juce_add_plugin (MidiJaQ
    COMPANY_NAME "Marbelite"
    BUNDLE_ID "com.marbelite.midijaq"
    PLUGIN_MANUFACTURER_CODE Mrbl
    PLUGIN_CODE MjaQ
    FORMATS VST3 Standalone
    PRODUCT_NAME "MidiJaQ"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Tools)

set (MIDIJAQ_APP_SOURCES
    Source/app/Processor.cpp
    Source/app/Editor.cpp)

set (MIDIJAQ_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    MIDIJAQ_VERSION="${PROJECT_VERSION}")

target_sources (MidiJaQ PRIVATE ${MIDIJAQ_APP_SOURCES})
target_include_directories (MidiJaQ PRIVATE Source)
target_compile_definitions (MidiJaQ PUBLIC ${MIDIJAQ_DEFINITIONS})
target_link_libraries (MidiJaQ
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Framework-free tests: the sequencer core and the clock, on the exact code the plugin uses.
find_package (Threads REQUIRED)
foreach (t test_jaq test_clock test_session test_evolving test_orb test_room)
    add_executable (${t} tools/${t}.cpp)
    target_include_directories (${t} PRIVATE Source)
    target_link_libraries (${t} PRIVATE Threads::Threads)
endforeach()

# Reading and playing a .NJQ instrument needs JUCE (the file format), so this one is a console app.
juce_add_console_app (test_inst PRODUCT_NAME "test_inst")
target_sources (test_inst PRIVATE tools/test_inst.cpp)
target_include_directories (test_inst PRIVATE Source)
target_compile_definitions (test_inst PRIVATE ${MIDIJAQ_DEFINITIONS})
target_link_libraries (test_inst PRIVATE juce::juce_audio_formats juce::juce_events juce::juce_recommended_config_flags)

# Linux only: the plugin driven as a host would drive it, and pictures of its window.
if (UNIX AND NOT APPLE AND MIDIJAQ_LINUX_TOOLS)
    foreach (tool HostTest Snapshot)
        juce_add_console_app (MidiJaQ${tool} PRODUCT_NAME "MidiJaQ${tool}")
        target_sources (MidiJaQ${tool} PRIVATE tools/${tool}.cpp ${MIDIJAQ_APP_SOURCES})
        target_include_directories (MidiJaQ${tool} PRIVATE Source)
        target_compile_definitions (MidiJaQ${tool} PRIVATE ${MIDIJAQ_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries (MidiJaQ${tool} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    endforeach()
endif()
