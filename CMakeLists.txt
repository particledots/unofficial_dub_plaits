cmake_minimum_required(VERSION 3.22)
project(UnofficialDubStabPlaits VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

option(DUB_TESTS_ONLY "Build only the headless tests (no JUCE, no plug-in)" OFF)

if(NOT DUB_TESTS_ONLY)
include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Plugin formats: AU + VST3 (+ Standalone for quick testing).
set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

# An instrument (no audio input) that takes MIDI: it shows up in Logic's normal Instrument slot.
juce_add_plugin(DubStabPlaits
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE DbSt
  FORMATS ${FORMATS}
  PRODUCT_NAME "Unofficial Dub Stab Plaits"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE)
endif()

# --- JUCE-free core (sequencer, transport clock, later the sound models), so the tests run headless ---
set(DUB_CORE_SOURCES src/core/Version.cpp src/core/DubSequencer.cpp src/core/TransportClock.cpp)
add_library(dub_core STATIC ${DUB_CORE_SOURCES})
target_include_directories(dub_core PUBLIC src)
set_target_properties(dub_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# No fused multiply-add and no fast-math: the sequencer must give the same numbers as the JavaScript original
# (Apple clang would otherwise fuse a*b+c on arm64, which the original does not do).
if(MSVC)
  target_compile_options(dub_core PRIVATE /fp:precise)
else()
  target_compile_options(dub_core PRIVATE -ffp-contract=off)
endif()

if(NOT DUB_TESTS_ONLY)
target_sources(DubStabPlaits PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_compile_definitions(DubStabPlaits PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(DubStabPlaits PRIVATE
  dub_core
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# --- Processor-level test (no host or window needed):  build/plugin_test ---
add_executable(plugin_test EXCLUDE_FROM_ALL tests/plugin_test.cpp)
target_include_directories(plugin_test PRIVATE src)
target_compile_definitions(plugin_test PRIVATE JucePlugin_Name="Unofficial Dub Stab Plaits" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(plugin_test PRIVATE DubStabPlaits juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()

# --- Sequencer cross-check against the original JavaScript:  build/seq_crosscheck tests/scenarios.txt > actual.txt
#     (CI runs the original with node on the same scenarios and compares the two logs line by line) ---
add_executable(seq_crosscheck EXCLUDE_FROM_ALL tests/seq_crosscheck.cpp)
target_link_libraries(seq_crosscheck PRIVATE dub_core)

# --- Transport tests (start, stop, loop jump, tempo change, block sizes), JUCE-free:  build/transport_test ---
add_executable(transport_test EXCLUDE_FROM_ALL tests/transport_test.cpp)
target_link_libraries(transport_test PRIVATE dub_core)
