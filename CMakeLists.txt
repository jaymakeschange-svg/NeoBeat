cmake_minimum_required (VERSION 3.22)
project (Batchcut VERSION 3.0.0 LANGUAGES C CXX)

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

juce_add_plugin (Batchcut
    COMPANY_NAME "Marbelite"
    BUNDLE_ID "com.marbelite.batchcut"
    PLUGIN_MANUFACTURER_CODE Mrbl
    PLUGIN_CODE Bcut
    FORMATS VST3 Standalone
    PRODUCT_NAME "Batchcut"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer Tools)

set (BATCHCUT_APP_SOURCES
    Source/app/MaterialRoom.cpp
    Source/app/Processor.cpp
    Source/app/Editor.cpp
    Source/app/Display.cpp
    Source/app/Panels.cpp
    Source/neobeat/shared/rooms/CutRooms.cpp
    Source/neobeat/shared/rooms/KitRooms.cpp
    Source/neobeat/shared/rooms/PresetBar.cpp
    Source/neobeat/shared/rooms/KitRoomUi.cpp
    Source/neobeat/shared/rooms/LoopsRooms.cpp
    Source/neobeat/shared/rooms/LoopsRoomUi.cpp
    Source/neobeat/shared/rooms/KeysRooms.cpp
    Source/neobeat/shared/rooms/KeysRoomUi.cpp)

set (BATCHCUT_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    BATCHCUT_VERSION="${PROJECT_VERSION}")

target_sources (Batchcut PRIVATE ${BATCHCUT_APP_SOURCES})
target_include_directories (Batchcut PRIVATE Source)
target_compile_definitions (Batchcut PUBLIC ${BATCHCUT_DEFINITIONS})
target_link_libraries (Batchcut
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Framework-free tests: the engine (detection, layers, loudness, player), the session (files, parts, export)
# and the detection benchmark, all on the exact code the plugin uses.
find_package (Threads REQUIRED)
foreach (t test_engine test_session test_kit test_gen test_loops test_keys bench_detect)
    add_executable (${t} tools/${t}.cpp)
    target_include_directories (${t} PRIVATE Source)
    target_link_libraries (${t} PRIVATE Threads::Threads)
endforeach()

# The shared groundwork: the .NJQ manifest, categories and the assigned folders. This one needs JUCE itself
# (zips, XML, files), unlike the tests above, so it is a console app rather than a bare executable. It runs
# everywhere the plugin builds, and writes nothing outside its own temporary folder.
# MaterialLab: the learner's material files played as resonant bodies. Needs JUCE for JSON and
# files, so it is a console app like test_shared.
juce_add_console_app (test_material PRODUCT_NAME "test_material")
target_sources (test_material PRIVATE tools/test_material.cpp)
target_include_directories (test_material PRIVATE Source)
target_compile_definitions (test_material PRIVATE ${BATCHCUT_DEFINITIONS} JUCE_STANDALONE_APPLICATION=1)
target_link_libraries (test_material PRIVATE juce::juce_audio_formats juce::juce_recommended_config_flags)

juce_add_console_app (test_shared PRODUCT_NAME "test_shared")
target_sources (test_shared PRIVATE tools/test_shared.cpp)
target_include_directories (test_shared PRIVATE Source)
target_compile_definitions (test_shared PRIVATE ${BATCHCUT_DEFINITIONS} JUCE_STANDALONE_APPLICATION=1)
target_link_libraries (test_shared PRIVATE juce::juce_audio_formats juce::juce_recommended_config_flags)

# Linux only: the host test (the plugin driven as a host would, with its editor) and editor snapshots.
if (UNIX AND NOT APPLE AND BATCHCUT_LINUX_TOOLS)
    foreach (tool HostTest Snapshot MakePatches)
        juce_add_console_app (Batchcut${tool} PRODUCT_NAME "Batchcut${tool}")
        target_sources (Batchcut${tool} PRIVATE tools/${tool}.cpp ${BATCHCUT_APP_SOURCES})
        target_include_directories (Batchcut${tool} PRIVATE Source)
        target_compile_definitions (Batchcut${tool} PRIVATE ${BATCHCUT_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries (Batchcut${tool} PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    endforeach()
endif()
