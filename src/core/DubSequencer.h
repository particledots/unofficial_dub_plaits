// DubSequencer: C++ port of dubstab.js, the sequencer and harmony of the DUB STAB Max for Live devices
// (Narti Live, MIT licence, Copyright (c) 2026 Narti Live), which in turn is a port of the DUB STAB PLAITS
// firmware (dub.cc). See licenses/CREDITS.md.
//
// Same random numbers (mulberry32 with the same seeding), same pattern recipe, same chords, same event order as the
// JavaScript original. tests/reference/ runs the original under node on the same scenarios and the CI compares the
// event logs line by line, so any difference in this file shows up as a red build.
//
// JUCE-free and allocation-light: no JUCE headers, so it also builds in the headless tests.
//
// The JavaScript's two output modes are kept:
//   Mode::Midi  "Dub Stab MIDI":  notes go out as MIDI note-on / note-off (gate length from the Gate setting),
//   Mode::Both  "Dub Stab Plaits": the sequencer plays the synth directly (Gen events; the synth shapes the notes).
// Max-specific bits that make no sense in a plug-in are left out (outlets for number boxes, NEW button debounce,
// the gen~ event-queue slot numbers).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dub {

enum class Mode { Midi, Both };

// The order the steps are played in (an addition to the original, which only plays forward). Forward is the default and
// is exactly the original; every other order is a pure function of the song position and the seed, so a loop in the host
// comes round the same way each time and a relocation lands on the same step as playing up to it.
enum Direction
{
    DirForward, DirReverse, DirPendulum, DirPingPong, DirRandom, DirRandomNoRepeat, DirDrunk, DirChance,
    kNumDirections
};
// The orders the Chance direction can pick from (index into Settings::dirWeight).
enum ChanceMode { ChForward, ChReverse, ChPendulum, ChPingPong, ChRandom, ChDrunk, kNumChanceModes };

// What the JavaScript sends out of its outlets.
struct Event
{
    enum Kind
    {
        MidiOn,      // note, vel
        MidiOff,     // note   (note may be outside 0..127, as in the original; the plug-in ignores those)
        Gen,         // type, note, vel   type: 0 chord note, 1 acid note, 2 acid note with slide, 3 acid gate off
        AcidLen,     // ms: gate length of the acid note about to be sent
        ChordText,   // text: what is playing (only when emitText is on)
        PatternText  // text: the 16-step display with the current step marked (only when emitText is on)
    };
    Kind kind = MidiOn;
    double tick = 0;     // grid tick (480 per quarter note) at which it happened
    int note = 0, vel = 0, type = 0;
    double ms = 0;
    std::string text;
};

struct Settings
{
    int root = 0;        // 0..11 = C..B
    int scale = 0;       // 0 natural minor, 1 Dorian, 2 Phrygian, 3 harmonic minor
    int minor = 0;       // 1 = only the scale's minor / diminished chords
    int voicing = 0;     // 0 DUB, 1 triads, 2 sevenths, 3 mixed, 4 DEEP
    int output = 0;      // Mode::Midi only: 0 chords, 1 acid line, 2 roots
    int density = 5;     // 0..16 steps that play
    int length = 16;     // 1..64 pattern length ("Steps"); the original has 1..16, which is the first bar of 16 steps
    int direction = 0;   // Direction (0 = forward = the original)
    int dirWeight[kNumChanceModes] = { 100, 60, 40, 0, 20, 0 };   // Direction::Chance: relative chance of each order, 0..100
    int runMin = 4, runMax = 16;                                   // Direction::Chance: steps an order is kept for (1..64)
    int rate = 4;        // 0..7 = 1/2 1/4 1/8 1/8T 1/16 1/16T 1/32 1/32T
    int octave = 3;      // 1..5
    double gate = 0.5;   // 0.05..2 of a step (Mode::Midi)
    int seed = 1;        // 0..99999
    int melody = 0;      // 0..99999, 0 = the pattern's own melody
    int model = 0;       // Mode::Both: 7 = ACID
    int transpose = 0;   // in scale degrees; set from a played MIDI note
    int chords = 1;      // 0 = mono notes instead of chords
};

struct Pattern
{
    std::array<int, 16> degree{}, voicing{}, rank{}, accent{}, mono{}, acidShift{};
};

struct Chord
{
    std::array<int, 6> notes{};      // ascending, first `count` are valid (a chord has at most 5 notes)
    int count = 0;
    int root = 0, degree = 0, voicing = 0;
};

class DubSequencer
{
public:
    static constexpr int kNumSteps = 16;            // steps in one bar of the pattern recipe (the original's whole pattern)
    static constexpr int kSections = 4;             // bars: bar 1 is the original's pattern, bars 2-4 are new patterns from the same seed
    static constexpr int kMaxSteps = kNumSteps * kSections;
    static constexpr int kGrid = 20;                // the original's clock runs every 20 ticks (480 per quarter)
    static constexpr int kDeepVoicing = 4;          // Voicing menu index of DEEP

    DubSequencer();                                  // like loading the script: generates the pattern for seed 1

    // --- settings (names and ranges as in the JavaScript inlet messages) ---
    void setMode (Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }
    void setRoot (double v);
    void setScale (double v);
    void setMinor (double v);
    void setVoicing (double v);
    void setOutput (double v);       // also releases held notes
    void setDensity (double v);
    void setSteps (double v);
    void setDirection (double v);
    void setDirWeight (int mode, double v);   // mode: ChanceMode
    void setRunMin (double v);
    void setRunMax (double v);
    void setRate (double v);
    void setOctave (double v);
    void setGate (double v);
    void setSeed (double v);
    void setMelody (double v);
    void setModel (double v);        // leaving ACID (7) closes the acid gate
    void setChords (double v);       // also releases held notes
    void setTempo (double bpm) { if (bpm > 0) tempoBpm_ = bpm; }
    void noteIn (int midiNote);      // a played note transposes the pattern within the scale (C3 = 60 = no change)

    // --- clock ---
    // Call with the transport position in ticks (480 per quarter); only changes of the 20-tick grid do anything.
    void ticks (double t);
    // Transport stopped or relocated: releases held notes, forgets the previous step.
    void state (int playing);

    // --- output ---
    std::vector<Event>& events() { return events_; }     // appended to; the caller clears it after reading
    bool emitText = false;                                // build ChordText / PatternText events (allocates; leave off in the audio thread)

    // --- read access (editor display, tests) ---
    const Settings& settings() const { return S; }
    const Pattern& pattern() const { return sections_[0]; }                    // bar 1 (the original's pattern)
    const Pattern& patternSection (int section) const { return sections_[(size_t) std::max (0, std::min (kSections - 1, section))]; }
    // Which step plays at step count n (n = song position in ticks / ticks per step, from 0), for the current Steps / Direction.
    int stepForIndex (long long n) const;
    bool stepPlays (int step) const;                      // step 0..63: does it sound at the current Density
    bool stepAccented (int step) const;
    void generate();                                       // rebuild the pattern from the current seed / melody
    static std::string patternText (const Settings&, const Pattern&, int currentStep);   // one bar of 16
    static int rateTicks (int rate);                       // ticks (480 per quarter) per step for a Rate setting 0..7
    Chord buildChord (int degree, int voicing) const;
    int semitonesToDegrees (int semitones) const;
    int monoNote (int degree, int step) const;             // step 0..63
    std::string chordName (const Chord&) const;
    static std::string noteName (int note);

private:
    struct Pending { double tick; int note; };

    // random
    void seedRandom (double s);
    double rand01();

    // harmony
    int scaleNote (int d) const;
    bool isMinorDegree (int d) const;
    int allowedDegree (int d) const;
    int keyOffset() const;
    int pickDegree (int avoid);
    struct Acid { int note; bool slide; };
    Acid acidNote (int degree, int step) const;            // step 0..63
    std::string deepName (const Chord&) const;

    // output
    void showPattern (int current);
    void push (Event e);
    void midiNoteOn (int note, int vel);
    void scheduleOff (int note, double tick);
    void processOffs (double tick);
    void allOff();
    void genEvent (int type, int note, int vel);
    void fireStep (int st, double g, int rt);
    void setInt (int& field, double v, int lo, int hi, bool regen);

    void generatePattern (Pattern& out, int section);
    Settings S;
    std::array<Pattern, kSections> sections_{};
    Mode mode_ = Mode::Midi;
    uint32_t rngState_ = 1;
    std::vector<Pending> pendingOffs_;
    double tempoBpm_ = 120;
    double lastGrid_ = -1;
    bool lastTickFired_ = false;
    double now_ = 0;                 // tick stamped on events that do not come from ticks()
    std::vector<Event> events_;
};

// JavaScript's Math.round (halves round up)
double jsRound (double x);

} // namespace dub
