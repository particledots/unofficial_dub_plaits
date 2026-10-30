// DirectPlay: what incoming MIDI notes do when the plug-in is set to "play notes directly" instead of transposing the
// pattern. Written for this plug-in (the module has no such mode; the Max devices do not sound the played note either).
//
// Poly models and STRINGS (stabs):
//   - note-ons that arrive within a few milliseconds of each other are one chord and make ONE stab. This is what lets a
//     MIDI effect that sends a chord (all notes at the same time, or a few samples apart) drive the sound.
//   - a note played while others are held makes a new stab with the new note(s); the held ones keep ringing (poly models)
//     or are included in the new chord (STRINGS, which plays one chord at a time).
//   - like the module, a note makes a stab whose length is set by ENVELOPE; releasing the key does not cut it. The
//     note-off only removes the note from the held set, so it is not part of the next chord.
//   - at most 5 notes per stab (the module's limit): the lowest five.
//   - velocity 100 or more = accent (the same rule as the sequencer's accents).
// ACID: mono, last-note priority. A note held while another is pressed slides; releasing the last held key closes the gate;
//   releasing the sounding key while others are held goes back to the most recent one (with a slide).
//
// JUCE-free; no allocation in the audio path as long as fewer than 512 commands are produced between two flush() calls.
#pragma once
#include <array>
#include <vector>

namespace dub {

struct DirectCmd
{
    enum Kind { Chord, AcidNote, AcidSlide, AcidOff };
    Kind kind = Chord;
    long long at = 0;                 // host sample (absolute, same time base as the note calls)
    std::array<int, 5> notes {};      // Chord: ascending
    int count = 0;
    int root = 60;                    // Chord: lowest held / played note (the sub-bass follows it)
    int note = 0;                     // Acid
    int vel = 0;                      // 127 or 90 (>= 100 is an accent)
};

class DirectPlay
{
public:
    static constexpr int kMaxChordNotes = 5;
    static constexpr int kMaxHeld = 16;

    DirectPlay();
    void prepare (double sampleRate);          // sets the 2 ms chord window; also resets
    void reset();                              // forget everything held; an ACID note that sounds is closed at the next flush()
    void setModelKind (bool acid, bool strings) { acid_ = acid; strings_ = strings; }

    void noteOn (long long at, int note, int velocity);
    void noteOff (long long at, int note);
    void allNotesOff (long long at);

    // Call once per block after the block's notes: closes a chord that is due and hands over the commands that fall before
    // blockEnd, in time order, clamped to start no earlier than blockStart. `out` is appended to.
    void flush (long long blockStart, long long blockEnd, std::vector<DirectCmd>& out);

    int heldCount() const { return nHeld_; }
    long long windowSamples() const { return window_; }

private:
    struct Held { int note; int vel; };
    void addHeld (int note, int vel);
    void closeGroup (long long due);
    void push (const DirectCmd& c) { if (ready_.size() < 512) ready_.push_back (c); }

    Held held_[kMaxHeld] {};
    int nHeld_ = 0;
    bool groupOpen_ = false;
    long long groupStart_ = 0;
    int group_[kMaxHeld] {};
    int nGroup_ = 0;
    int groupVel_ = 0;
    bool acid_ = false, strings_ = false;
    int acidSounding_ = -1;
    bool pendingAcidOff_ = false;
    long long window_ = 96;
    std::vector<DirectCmd> ready_;
};

} // namespace dub
