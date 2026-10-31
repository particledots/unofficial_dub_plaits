cmake_minimum_required(VERSION 3.22)
project(UnofficialDubStabPlaits VERSION 0.3.0)

# The About panel shows the credits and licence notices. The text is read from licenses/ and LICENSE here, at configure time,
# so the panel can never differ from the files that ship in the zip.
file(READ licenses/CREDITS.md DUB_CREDITS_TEXT)
file(READ licenses/NARTI-LIVE-LICENSE.txt DUB_NARTI_LICENSE_TEXT)
file(READ LICENSE DUB_PARTICLEDOTS_LICENSE_TEXT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS licenses/CREDITS.md licenses/NARTI-LIVE-LICENSE.txt LICENSE src/AboutText.h.in)
configure_file(src/AboutText.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/AboutText.h @ONLY)

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

# --- Vendored sources of the sound models: Narti Live's dub.cc, the Plaits string machine, stmlib (see third_party/VERSIONS.txt) ---
set(DUB_PLAITS_SOURCES
  third_party/plaits/dub/dub.cc
  third_party/plaits/dsp/engine2/string_machine_engine.cc
  third_party/plaits/dsp/chords/chord_bank.cc
  third_party/plaits/resources.cc
  third_party/stmlib/utils/random.cc
  third_party/stmlib/dsp/units.cc)
add_library(dub_plaits STATIC ${DUB_PLAITS_SOURCES})
target_include_directories(dub_plaits PUBLIC third_party)
# TEST=1 is the upstream desktop-build switch: it turns the STM32 flash / assembly code into portable stand-ins.
target_compile_definitions(dub_plaits PUBLIC TEST=1)
set_target_properties(dub_plaits PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Some upstream headers rely on <cstdio>/<cstdlib>/<cmath>/<cstdint> being pulled in transitively.
if(MSVC)
  target_compile_options(dub_plaits PRIVATE /FIcstdio /FIcstdlib /FIcmath /FIcstdint)
else()
  target_compile_options(dub_plaits PRIVATE "SHELL:-include cstdio" "SHELL:-include cstdlib" "SHELL:-include cmath" "SHELL:-include cstdint"
    -Wno-unused-local-typedef -Wno-unused-variable)
endif()

# --- JUCE-free core (sequencer, transport clock, the synth), so the tests run headless ---
set(DUB_CORE_SOURCES src/core/Version.cpp src/core/DubSequencer.cpp src/core/TransportClock.cpp
                     src/core/AcidVoice.cpp src/core/DubSynth.cpp
                     src/core/Effects.cpp src/core/DirectPlay.cpp src/core/PatternExport.cpp)
add_library(dub_core STATIC ${DUB_CORE_SOURCES})
target_include_directories(dub_core PUBLIC src)
target_link_libraries(dub_core PUBLIC dub_plaits)
set_target_properties(dub_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# No fused multiply-add and no fast-math: the sequencer must give the same numbers as the JavaScript original
# (Apple clang would otherwise fuse a*b+c on arm64, which the original does not do).
if(MSVC)
  target_compile_options(dub_core PRIVATE /fp:precise)
else()
  target_compile_options(dub_core PRIVATE -ffp-contract=off)
endif()

if(NOT DUB_TESTS_ONLY)
target_sources(DubStabPlaits PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp src/Presets.cpp src/MidiFileExport.cpp)
target_include_directories(DubStabPlaits PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
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
target_link_libraries(plugin_test PRIVATE DubStabPlaits dub_core juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()

# --- Sequencer cross-check against the original JavaScript:  build/seq_crosscheck tests/scenarios.txt > actual.txt
#     (CI runs the original with node on the same scenarios and compares the two logs line by line) ---
add_executable(seq_crosscheck EXCLUDE_FROM_ALL tests/seq_crosscheck.cpp)
target_link_libraries(seq_crosscheck PRIVATE dub_core)

# --- Transport tests (start, stop, loop jump, tempo change, block sizes), JUCE-free:  build/transport_test ---
add_executable(transport_test EXCLUDE_FROM_ALL tests/transport_test.cpp)
target_link_libraries(transport_test PRIVATE dub_core)

# --- Sound model tests (every model, 3 sample rates; finite / bounded / not silent; block sizes; switching; the whole
#     sequencer -> clock -> synth chain), JUCE-free:  build/synth_test ---
add_executable(synth_test EXCLUDE_FROM_ALL tests/synth_test.cpp)
target_link_libraries(synth_test PRIVATE dub_core)

# --- Effects (echo, reverb, width), JUCE-free:  build/fx_test ---
add_executable(fx_test EXCLUDE_FROM_ALL tests/fx_test.cpp)
target_link_libraries(fx_test PRIVATE dub_core)

# --- Direct play (notes -> stabs), the synth's direct chords and sub-bass output, the pattern exporter, JUCE-free:  build/direct_test ---
add_executable(direct_test EXCLUDE_FROM_ALL tests/direct_test.cpp)
target_link_libraries(direct_test PRIVATE dub_core)

# --- Steps 17-64 and the Direction orders (the additions to the original), JUCE-free:  build/direction_test ---
add_executable(direction_test EXCLUDE_FROM_ALL tests/direction_test.cpp)
target_link_libraries(direction_test PRIVATE dub_core)

# --- Listening aid: one WAV per sound model played by the real sequencer (CI uploads them):  build/render_demos <folder> ---
add_executable(render_demos EXCLUDE_FROM_ALL tools/render_demos.cpp)
target_link_libraries(render_demos PRIVATE dub_core)
