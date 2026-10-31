// PatternExport: the notes the current pattern plays, as a list, for the MIDI file the editor can drag into Logic.
//
// It runs a separate DubSequencer in its "MIDI out" mode (the "Dub Stab MIDI" device of the original) from song position 0
// for a number of bars, so the notes are exactly what the plug-in plays when the song starts at bar 1 (the pattern is a
// function of the song position, so a region that starts at bar 1 lines up with it).
// JUCE-free.
#pragma once
#include <vector>
#include "DubSequencer.h"

namespace dub {

struct ExportSettings
{
    int root = 0, scale = 0, minor = 0, voicing = 0;
    int density = 5, steps = 16, rate = 4, octave = 3;
    int seed = 1, melody = 0;
    int model = 0;              // 7 (ACID) exports the acid line instead of the chords
    int chords = 1;             // 0 = single notes
    int transposeNote = -1;     // the MIDI note currently transposing the pattern (60 = none), -1 = no note played
    double gate = 0.5;          // note length as a fraction of one step (0.05 .. 2)
    int bars = 4;               // 4/4 bars from song position 0
    int direction = 0;          // Direction: the order the steps are played in (0 = forward)
    int dirWeight[kNumChanceModes] = { 100, 60, 40, 0, 20, 0 };   // Direction::Chance
    int runMin = 4, runMax = 16;                                   // Direction::Chance
};

struct ExportNote
{
    long long startTick = 0;    // 480 ticks per quarter note
    long long endTick = 0;
    int note = 60;
    int velocity = 90;
    bool operator== (const ExportNote& o) const { return startTick == o.startTick && endTick == o.endTick && note == o.note && velocity == o.velocity; }
};

// Notes sorted by start tick (then pitch). Every note ends at or before bars * 1920.
std::vector<ExportNote> renderPatternNotes (const ExportSettings&);

constexpr int kTicksPerQuarter = 480;

} // namespace dub
