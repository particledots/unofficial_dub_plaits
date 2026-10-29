// TransportClock: turns a host's transport (position in quarter notes, tempo, playing) and audio block size into
// sample-accurate sequencer events. It plays the part of Max's clock in the Dub Stab Max for Live devices: it calls
// DubSequencer::ticks() for every point of the 20-tick grid (480 ticks per quarter note) that falls inside the block,
// and works out in which sample of the block that point lies.
//
//   - start / stop: stopping releases held notes; stopped transport gives no events at all (silent)
//   - position jump while playing (loop, locate): held notes are released, then the pattern follows the new
//     position. Which step plays is a function of the song position only, so a jump never puts the pattern out of step.
//   - tempo change: ticks do not depend on tempo; only their sample positions (and the acid gate length) do
//   - negative positions (count-in): nothing until the song position reaches 0
//
// JUCE-free so the tests run headless.
#pragma once
#include "DubSequencer.h"
#include <vector>

namespace dub {

struct HostBlock
{
    bool playing = false;          // transport running
    bool positionValid = true;     // false if the host gave no position (treated like stopped)
    double ppqStart = 0;           // song position at the first sample of the block, in quarter notes
    double bpm = 120;
    double sampleRate = 48000;
    int numSamples = 0;
};

struct TimedEvent
{
    int sample = 0;                // 0 .. numSamples-1 within the block
    Event ev;
};

class TransportClock
{
public:
    void reset() { running_ = false; prevEnd_ = 0; nextK_ = 0; }
    bool running() const { return running_; }

    // Appends this block's events to `out` (the caller clears it between blocks).
    void process (DubSequencer& seq, const HostBlock& block, std::vector<TimedEvent>& out);

private:
    void drain (DubSequencer& seq, int sample, std::vector<TimedEvent>& out);

    bool running_ = false;
    double prevEnd_ = 0;           // song position (ticks) where the previous block ended
    long long nextK_ = 0;          // index of the next grid point (tick = 20 * k) still to fire
};

} // namespace dub
