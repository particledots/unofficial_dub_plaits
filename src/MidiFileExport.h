// Writes the notes of the current pattern (dub::renderPatternNotes) as a standard MIDI file, 480 ticks per quarter note,
// starting at song position 0, with a tempo marker and an end marker at the end of the last bar so the region keeps its length.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "core/PatternExport.h"

// Returns false if the file could not be written.
bool writePatternMidiFile (const dub::ExportSettings& settings, double bpm, const juce::String& trackName, const juce::File& file);
