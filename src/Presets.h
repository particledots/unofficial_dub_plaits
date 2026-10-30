// Presets: factory presets (built in) and user presets (XML files in the user's Application Support folder).
//
// A preset holds the sound and the pattern; it never touches the "setup" parameters (MIDI note mode, free-run on / tempo,
// the MIDI export length), so choosing a preset does not suddenly stop the plug-in from being driven by your MIDI effect.
// Loading a preset first puts every preset parameter back to its default, then applies the values the preset lists.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace presets {

struct Value { const char* id; float value; };      // plain (not 0..1) values; choices and switches by index / 0 / 1
struct Factory { const char* name; std::vector<Value> values; };

const std::vector<Factory>& factory();

// False for the setup parameters a preset never changes.
bool isPresetParameter (const juce::String& parameterId);

// Sets every preset parameter to its default, then the listed values.
void apply (juce::AudioProcessorValueTreeState&, const std::vector<Value>& values);

juce::File userFolder();                                          // ~/Library/Application Support/particledots/Unofficial Dub Stab Plaits/Presets
juce::Array<juce::File> userFiles (const juce::File& folder);     // *.dubstabpreset, sorted by name
juce::String cleanName (const juce::String& typed);               // what a typed name becomes (no slashes etc.); empty if nothing is left
bool saveUser (juce::AudioProcessorValueTreeState&, const juce::String& name, const juce::File& folder, juce::File* written = nullptr);
bool loadUser (juce::AudioProcessorValueTreeState&, const juce::File& file, juce::String* nameOut = nullptr);

} // namespace presets
